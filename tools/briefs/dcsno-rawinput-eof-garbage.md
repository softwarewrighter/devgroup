# Brief: SNOBOL4 RAWINPUT returns one record of binary garbage past real EOF

**Owner:** dcsno
**Branch:** `pr/rawinput-eof-garbage`
**Repo:** `sw-cor24-snobol4`
**Discovered by:** dcftn during `milestone-12-inline-runtime` saga (2026-05-14).
**Affects:** the 2026-05-14 07:23 `work/lib/cor24/snobol4.lgo` build
(md5 `f7aa430c`); not present in earlier builds. Likely introduced
by one of the four cap-raising fixes merged that morning
(`pr/source-byte-cap`, `pr/any-pattern-fails`,
`pr/concat-truncation`, `pr/pattern-captures-truncation`).

## Symptom

After all real input bytes have been read, `L = RAWINPUT :F(LBL)`
returns ONE MORE record containing binary garbage (looks like
uninitialised buffer / heap noise) and SUCCEEDS, advancing to
the next statement instead of taking `:F(LBL)`. The call after
that fails properly.

Real-world hit: the dcftn FTI-0 compiler's `normalize.sno` reads
fixed-form `.f` source line-by-line. After the last source line,
the spurious garbage read is processed as if it were a Fortran
continuation line and its bytes get appended to the final
statement's text. Downstream `classify.sno` then chokes
because its OWN RAWINPUT loop (over normalize's output) also
hits the garbage trailer AND because the binary bytes in the
intermediate file trigger a separate regression (see
`dcsno-rawinput-mid-file-halt.md`). Net result: the entire
FTI-0 pipeline (which had 10 working demos on dev as of
2026-05-13) produces empty `.s` files on dev today.

## Minimal repro

```
$ cat > /tmp/loop.sno << 'EOF'
        N = 0
RD      L = RAWINPUT                            :F(FL)
        N = N + 1
        OUTPUT = 'rec' N ' first=[' SUBSTR(L,1,1) ']'
        :(RD)
FL      OUTPUT = 'done, reads=' N
END
EOF

$ printf 'line one\nline two\nline three\n' > /tmp/three.txt

$ snobol4 --load-binary "/tmp/loop.sno@0x080000" \
          --load-binary "/tmp/three.txt@0x090000" \
          --entry 0 --quiet --speed 0 -n 1000000 -t 30
```

Expected (3 newline-terminated lines = 3 records):
```
rec1 first=[l]
rec2 first=[l]
rec3 first=[l]
done, reads=3
```

Observed (`f7aa430c` build):
```
rec1 first=[l]
rec2 first=[l]
rec3 first=[l]
rec4 first=[<binary>]   <- garbage record, RAWINPUT didn't fail here
done, reads=4
```

The garbage record's content varies per run -- looks like data
read off the end of the loaded buffer into adjacent
(uninitialised? or some other section's?) memory.

## Hypothesis

EOF check in the RAWINPUT routine may use a stale "current
position vs end-of-data" comparison that's off-by-one-line, OR
the line-terminator scan is reading past the loaded data into
adjacent memory and only failing on the read after, when the
scan crosses some unmapped / sentinel byte.

## Fix shape

In whichever routine implements RAWINPUT (probably
`src/sno_io.plsw` or similar), gate the read on:

  if (current_position >= load_end_address) -> fail (:F path).

Check this BEFORE scanning for the next newline, not after.

## Tests dcsno should add (regression coverage that would have
## caught this)

The four fixes that landed 2026-05-14 each shipped with a
fix-specific test, but no broader I/O smoke test. Suggested
additions:

1. **EOF-exactness test:**

   ```sno
            STMT = 0
   RD       L = RAWINPUT  :F(DONE)
            STMT = STMT + 1
            :(RD)
   DONE     OUTPUT = 'reads=' STMT
   END
   ```

   For a data file with N newline-terminated lines, assert
   the program outputs exactly `reads=N`.

2. **Empty-input test:** zero-byte data file; assert
   `reads=0`.

3. **Round-trip:** write N records via OUTPUT to a file,
   re-load that file as data, read via RAWINPUT; assert
   byte-equality of each record.

4. **Vary sizes:** N in {0, 1, 2, 3, 5, 10, 50, 100, 500}.
   N in {0, 1, 2} catches off-by-one EOF detection; larger
   N catches buffer-related issues.

## When done

Push `pr/rawinput-eof-garbage`. After mike relays + reinstalls
`snobol4.lgo`, dcftn re-verifies all 10 FTI-0 demos on dev.
