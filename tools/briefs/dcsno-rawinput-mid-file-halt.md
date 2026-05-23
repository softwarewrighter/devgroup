# Brief: SNOBOL4 RAWINPUT silently halts (no :S, no :F) on certain mid-size inputs

**Owner:** dcsno
**Branch:** `pr/rawinput-mid-file-halt`
**Repo:** `sw-cor24-snobol4`
**Discovered by:** dcftn during `milestone-12-inline-runtime` saga (2026-05-14).
**Affects:** the 2026-05-14 07:23 `work/lib/cor24/snobol4.lgo` build
(md5 `f7aa430c`); not present in earlier builds.

## Symptom

When the data buffer (loaded via `--load-binary @0x090000`) is
in a certain size range with a certain line-length shape,
`L = RAWINPUT :F(LBL)` fails on the FIRST call: it does not
fire `:S`, does not fire `:F`, and execution effectively
appears to halt at that statement (the next statement never
runs).

Most clearly reproduced with two-line input where the first
line is ~100 chars and the second is ~50 chars, giving a file
of ~152 bytes. The threshold isn't pure file size -- shorter or
longer files often work -- it's specifically a structure-
dependent failure mode that bites at ~150+ bytes with non-
uniform line lengths.

The original FTI-0 hit was a `normalize.sno` output file (154
bytes, four `stmt<N> line=<M> label= text=...` records with
binary-bytes-from-EOF-garbage in the last record). With that
file as data, `classify.sno` produces *zero* output -- its
RAWINPUT loop never fires `:S` or `:F` on the first call.

## Minimal repro

```
$ cat > /tmp/loop.sno << 'EOF'
        OUTPUT = 'before-rawinput'
        L = RAWINPUT                            :F(EOFL)
        OUTPUT = 'rawinput-success: [' L ']'
        :(DONE)
EOFL    OUTPUT = 'rawinput-failed-cleanly'
DONE    OUTPUT = 'after'
END
EOF

$ python3 -c "import sys; sys.stdout.buffer.write(b'A'*100 + b'\n' + b'B'*50 + b'\n')" > /tmp/big.txt
$ ls -l /tmp/big.txt
# -rw-r--r-- 1 ... 152 ... /tmp/big.txt

$ snobol4 --load-binary "/tmp/loop.sno@0x080000" \
          --load-binary "/tmp/big.txt@0x090000" \
          --entry 0 --quiet --speed 0 -n 100000 -t 5
```

Expected: either `rawinput-success: [AAAA...]` then `after`, OR
`rawinput-failed-cleanly` then `after`. Either way, both an
`after` and one of the inner messages.

Observed (`f7aa430c` build):
```
before-rawinput

Executed 55010 instructions
```

Just `before-rawinput`. No `rawinput-success`, no
`rawinput-failed-cleanly`, no `after`. The `L = RAWINPUT
:F(EOFL)` statement neither succeeded nor took the `:F` branch
-- as if the call silently exited the program. Cycle limit
wasn't hit (`-n 100000`, only ~55K consumed).

## Size scan

```
$ for N1 in 90 100 110; do
    for N2 in 30 40 50 60 70; do
      python3 -c "
import sys; sys.stdout.buffer.write(b'A'*$N1+b'\n'+b'B'*$N2+b'\n')
" > /tmp/tn.txt
      size=$(stat -c %s /tmp/tn.txt)
      out=$(snobol4 --load-binary /tmp/loop.sno@0x080000 \
                    --load-binary /tmp/tn.txt@0x090000 \
                    --entry 0 --quiet --speed 0 -n 100000 -t 5 \
            2>/dev/null | tail -1)
      echo "N1=$N1 N2=$N2 size=$size -> $out"
    done
  done
N1=90  N2=30 size=122 -> done N=3      (OK, R1 EOF-garbage)
N1=90  N2=40 size=132 -> done N=3      (OK)
N1=90  N2=50 size=142 -> done N=3      (OK)
N1=90  N2=60 size=152 ->                (BUG: zero output)
N1=90  N2=70 size=162 ->                (BUG: zero output)
N1=100 N2=30 size=132 -> done N=3      (OK)
N1=100 N2=40 size=142 -> done N=3      (OK)
N1=100 N2=50 size=152 ->                (BUG)
N1=100 N2=60 size=162 ->                (BUG)
N1=100 N2=70 size=172 -> rec10         (different bug -- splits into chunks)
N1=110 N2=30 size=142 -> done N=3      (OK)
N1=110 N2=40 size=152 ->                (BUG)
N1=110 N2=50 size=162 ->                (BUG)
```

Failure starts when total file size crosses ~150 bytes AND the
second line is long enough. Note also the N1=100 N2=70 case:
RAWINPUT enters some loop emitting `rec1`...`rec10+` until
`-n 100000` exhausts -- yet another fault mode.

## Hypothesis

Most likely: a fixed line-buffer in the RAWINPUT routine has
been resized as part of one of the cap-raise PRs landed
2026-05-14, but the boundary check on "scan for next newline"
hasn't been updated in lock-step. Long lines may overflow the
new buffer and a sentinel/pointer check after that causes the
routine to early-exit without firing either `:S` or `:F`.

The "rec1...rec10" splitting on a 200-char single line
strongly supports a buffer-of-~20-bytes-and-loop hypothesis.

## Fix shape

1. Identify the line buffer in `src/sno_io.plsw` (or wherever
   RAWINPUT lives).
2. Either resize it to handle any reasonable input line (FTI-0
   would benefit from 256 bytes), or implement proper
   line-too-long failure that fires `:F`.
3. Audit the bounds-check loops that scan for the next `\n` to
   ensure they always update both the read position AND the
   buffer-end check on every byte.
4. Most importantly: make sure RAWINPUT NEVER silently fails
   to fire :S/:F. The dispatch contract is part of SNOBOL4
   semantics; a failure that doesn't take :F is a hard bug
   that's nearly impossible to debug from the user side.

## Tests dcsno should add (regression coverage)

1. **Line-length matrix:** for every (line-length, line-count)
   in {10, 50, 100, 150, 200, 500} x {1, 2, 3, 5}, assert
   the program reads exactly `line-count` records (modulo R1).

2. **Big-buffer round-trip:** write 100 records of varying
   length via OUTPUT to a file, re-load as data, read back via
   RAWINPUT; assert all 100 records returned with correct
   bytes.

3. **:S / :F contract:** for every RAWINPUT call, *exactly
   one* of :S or :F must fire. A test harness can wrap each
   call as `L = RAWINPUT :S(GOT) :F(NOPE)` with both branches
   logged, and assert the total log count equals the call
   count.

4. **Don't ship without a CI gate on the dcftn FTI-0 demo
   set.** Those 10 demos are real SNOBOL4 workloads that
   already exercise RAWINPUT, OUTPUT, BREAK, SPAN, IDENT, and
   the goto dispatch in non-trivial combinations. Running them
   on every dcsno PR before merge would have caught this.

## When done

Push `pr/rawinput-mid-file-halt`. After mike relays, dcftn
re-verifies all 10 FTI-0 demos.
