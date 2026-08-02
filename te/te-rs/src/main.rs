use std::env;
use std::fs::{File, OpenOptions};
use std::io::{self, BufRead, BufReader, Read, Write};
use std::os::fd::{AsRawFd, RawFd};
use std::path::Path;
use std::process::ExitCode;
use std::thread;
use std::time::{Duration, Instant};

const DEFAULT_DEVICE: &str = "/dev/ttyUSB0";
const SYNC_TIMEOUT: Duration = Duration::from_secs(2);
const MAX_MONITOR_RESPONSE: usize = 1024;

// Keep monitor diagnostics in one place. Replace or extend these placeholders
// when the monitor's exact error messages are known.
const MONITOR_ERROR_PATTERNS: &[&str] = &["invalid", "too long"];

#[derive(Debug, PartialEq)]
struct Options {
    device: String,
    delay: Option<Duration>,
    byte_delay: Option<Duration>,
    sync: bool,
    verbose: bool,
}

enum ParseResult {
    Run(Options),
    Help,
}

fn help(program: &str) -> String {
    format!(
        "\
Usage: {program} [OPTIONS] [DEVICE]

Interactive COR24 serial terminal and paced .lgo file uploader.

Arguments:
  [DEVICE]              Serial device [default: {DEFAULT_DEVICE}]

Options:
  -d, --delay <MS>      Wait 1 to 10 milliseconds after each uploaded line
  -b, --byte-delay <US> Drain the UART and wait 1 to 10000 microseconds after
                        each uploaded byte
  -s, --sync            Validate each line's exact echo before sending the next
  -v, --verbose         Log serial setup and per-line upload details to stderr
  -h, --help            Print this help

The serial port is configured for 921600 baud, 8 data bits, no parity, one
stop bit, and RTS/CTS flow control. During a session, press Ctrl-R and enter a
filename to upload it. Press Ctrl-C to exit. With --sync, each line must be
echoed exactly within 2 seconds; a mismatch or timeout aborts the upload.
"
    )
}

fn parse_args<I>(args: I) -> Result<ParseResult, String>
where
    I: IntoIterator<Item = String>,
{
    let mut args = args.into_iter();
    let _program = args.next();
    let mut options = Options {
        device: DEFAULT_DEVICE.into(),
        delay: None,
        byte_delay: None,
        sync: false,
        verbose: false,
    };
    let mut device_seen = false;

    while let Some(arg) = args.next() {
        match arg.as_str() {
            "-h" | "--help" => return Ok(ParseResult::Help),
            "-v" | "--verbose" => options.verbose = true,
            "-s" | "--sync" => options.sync = true,
            "-d" | "--delay" => {
                let value = args
                    .next()
                    .ok_or_else(|| format!("{arg} requires a value in milliseconds"))?;
                let milliseconds: u64 = value.parse().map_err(|_| {
                    format!("invalid delay '{value}': expected an integer from 1 to 10")
                })?;
                if !(1..=10).contains(&milliseconds) {
                    return Err(format!(
                        "invalid delay '{value}': expected an integer from 1 to 10"
                    ));
                }
                options.delay = Some(Duration::from_millis(milliseconds));
            }
            "-b" | "--byte-delay" => {
                let value = args
                    .next()
                    .ok_or_else(|| format!("{arg} requires a value in microseconds"))?;
                let microseconds: u64 = value.parse().map_err(|_| {
                    format!("invalid byte delay '{value}': expected an integer from 1 to 10000")
                })?;
                if !(1..=10_000).contains(&microseconds) {
                    return Err(format!(
                        "invalid byte delay '{value}': expected an integer from 1 to 10000"
                    ));
                }
                options.byte_delay = Some(Duration::from_micros(microseconds));
            }
            _ if arg.starts_with('-') => return Err(format!("unknown option '{arg}'")),
            _ if !device_seen => {
                options.device = arg;
                device_seen = true;
            }
            _ => return Err(format!("unexpected argument '{arg}'")),
        }
    }

    Ok(ParseResult::Run(options))
}

struct TermiosGuard {
    fd: RawFd,
    original: libc::termios,
}

impl TermiosGuard {
    fn serial(fd: RawFd) -> io::Result<Self> {
        let original = get_termios(fd)?;
        let mut configured = original;
        // SAFETY: configured points to a valid termios structure.
        unsafe { libc::cfmakeraw(&mut configured) };
        configured.c_cflag |= libc::CRTSCTS;
        configured.c_iflag |= libc::IGNBRK;

        // SAFETY: configured is valid and B921600 is a supported speed constant
        // on Linux. Errors are reported through errno.
        if unsafe { libc::cfsetispeed(&mut configured, libc::B921600) } == -1
            || unsafe { libc::cfsetospeed(&mut configured, libc::B921600) } == -1
        {
            return Err(io::Error::last_os_error());
        }
        set_termios(fd, &configured)?;
        Ok(Self { fd, original })
    }

    fn terminal(fd: RawFd) -> io::Result<Self> {
        let original = get_termios(fd)?;
        let configured = terminal_attributes(original);
        set_termios(fd, &configured)?;
        Ok(Self { fd, original })
    }
}

fn terminal_attributes(mut attributes: libc::termios) -> libc::termios {
    // Raw mode makes Ctrl-R and Ctrl-C available to this process as bytes.
    // SAFETY: attributes points to a valid termios structure.
    unsafe { libc::cfmakeraw(&mut attributes) };
    // Keep terminal output readable: the COR24 sends LF line endings, so
    // translate each LF to CRLF instead of letting successive lines stair-step.
    attributes.c_oflag |= libc::OPOST | libc::ONLCR;
    attributes
}

impl Drop for TermiosGuard {
    fn drop(&mut self) {
        // Best-effort restoration during normal exit and error unwinding.
        let _ = set_termios(self.fd, &self.original);
    }
}

fn get_termios(fd: RawFd) -> io::Result<libc::termios> {
    // SAFETY: zero is a valid initial bit pattern and tcgetattr initializes it.
    let mut attributes = unsafe { std::mem::zeroed() };
    // SAFETY: attributes is writable and fd is expected to refer to a tty.
    if unsafe { libc::tcgetattr(fd, &mut attributes) } == -1 {
        Err(io::Error::last_os_error())
    } else {
        Ok(attributes)
    }
}

fn set_termios(fd: RawFd, attributes: &libc::termios) -> io::Result<()> {
    // SAFETY: attributes points to a valid termios structure.
    if unsafe { libc::tcsetattr(fd, libc::TCSANOW, attributes) } == -1 {
        Err(io::Error::last_os_error())
    } else {
        Ok(())
    }
}

fn prompt_for_file(tty: &mut File) -> io::Result<Option<String>> {
    tty.write_all(b"\r\nfile: ")?;
    tty.flush()?;
    let mut name = Vec::new();
    let mut byte = [0_u8; 1];

    loop {
        tty.read_exact(&mut byte)?;
        match byte[0] {
            b'\r' | b'\n' => {
                tty.write_all(b"\r\n")?;
                tty.flush()?;
                break;
            }
            0x03 => return Ok(None),
            0x08 | 0x7f if !name.is_empty() => {
                name.pop();
                tty.write_all(b"\x08 \x08")?;
                tty.flush()?;
            }
            0x08 | 0x7f => {}
            value => {
                name.push(value);
                tty.write_all(&[value])?;
                tty.flush()?;
            }
        }
    }

    Ok(Some(String::from_utf8_lossy(&name).into_owned()))
}

fn monitor_error(response: &[u8]) -> Option<&'static str> {
    let lowercase = String::from_utf8_lossy(response).to_ascii_lowercase();
    MONITOR_ERROR_PATTERNS
        .iter()
        .copied()
        .find(|pattern| lowercase.contains(pattern))
}

fn read_monitor_response_line(
    serial: &mut File,
    tty: &mut File,
    response: &mut Vec<u8>,
    deadline: Instant,
) -> io::Result<()> {
    let mut byte = [0_u8; 1];
    while response.len() < MAX_MONITOR_RESPONSE && !response.ends_with(b"\n") {
        let now = Instant::now();
        if now >= deadline {
            break;
        }
        let remaining = deadline.saturating_duration_since(now);
        let timeout_ms = remaining.as_millis().min(i32::MAX as u128) as i32;
        let mut descriptor = libc::pollfd {
            fd: serial.as_raw_fd(),
            events: libc::POLLIN,
            revents: 0,
        };
        // SAFETY: descriptor points to one valid pollfd.
        let ready = unsafe { libc::poll(&mut descriptor, 1, timeout_ms) };
        if ready == -1 {
            let error = io::Error::last_os_error();
            if error.kind() == io::ErrorKind::Interrupted {
                continue;
            }
            return Err(error);
        }
        if ready == 0 {
            break;
        }
        serial.read_exact(&mut byte)?;
        tty.write_all(&byte)?;
        tty.flush()?;
        response.push(byte[0]);
        if monitor_error(response).is_some() {
            break;
        }
    }
    Ok(())
}

fn wait_for_echo(serial: &mut File, tty: &mut File, expected: &[u8]) -> io::Result<()> {
    let deadline = Instant::now() + SYNC_TIMEOUT;
    let mut received = Vec::with_capacity(expected.len());
    let mut byte = [0_u8; 1];

    while received.len() < expected.len() {
        let now = Instant::now();
        if now >= deadline {
            return Err(io::Error::new(
                io::ErrorKind::TimedOut,
                format!(
                    "echo timeout after {} of {} bytes",
                    received.len(),
                    expected.len()
                ),
            ));
        }
        let remaining = deadline.saturating_duration_since(now);
        let timeout_ms = remaining.as_millis().min(i32::MAX as u128) as i32;
        let mut descriptor = libc::pollfd {
            fd: serial.as_raw_fd(),
            events: libc::POLLIN,
            revents: 0,
        };
        // SAFETY: descriptor points to one valid pollfd.
        let ready = unsafe { libc::poll(&mut descriptor, 1, timeout_ms) };
        if ready == -1 {
            let error = io::Error::last_os_error();
            if error.kind() == io::ErrorKind::Interrupted {
                continue;
            }
            return Err(error);
        }
        if ready == 0 {
            continue;
        }

        serial.read_exact(&mut byte)?;
        tty.write_all(&byte)?;
        tty.flush()?;
        let index = received.len();
        received.push(byte[0]);
        if byte[0] != expected[index] {
            read_monitor_response_line(serial, tty, &mut received, deadline)?;
            if let Some(pattern) = monitor_error(&received) {
                let message = String::from_utf8_lossy(&received);
                return Err(io::Error::new(
                    io::ErrorKind::InvalidData,
                    format!(
                        "monitor rejected input (matched '{pattern}'): {}",
                        message.trim()
                    ),
                ));
            }
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                format!(
                    "echo mismatch at byte {}: sent 0x{:02x}, received 0x{:02x}",
                    index + 1,
                    expected[index],
                    byte[0]
                ),
            ));
        }
    }
    Ok(())
}

fn drain_serial(fd: RawFd) -> io::Result<()> {
    loop {
        // SAFETY: fd refers to the open serial tty. tcdrain waits until all
        // queued output has physically transmitted.
        if unsafe { libc::tcdrain(fd) } == 0 {
            return Ok(());
        }
        let error = io::Error::last_os_error();
        if error.kind() != io::ErrorKind::Interrupted {
            return Err(error);
        }
    }
}

fn upload(path: &Path, serial: &mut File, tty: &mut File, options: &Options) -> io::Result<()> {
    let file = File::open(path)?;
    let mut reader = BufReader::new(file);
    let mut line = Vec::new();
    let mut line_number = 0_usize;

    loop {
        line.clear();
        if reader.read_until(b'\n', &mut line)? == 0 {
            break;
        }
        line_number += 1;
        if options.verbose {
            eprintln!(
                "upload: line {line_number}, {} bytes{}",
                line.len(),
                if options.sync { ", awaiting echo" } else { "" }
            );
        }

        if let Some(byte_delay) = options.byte_delay {
            for byte in &line {
                serial.write_all(std::slice::from_ref(byte))?;
                drain_serial(serial.as_raw_fd())?;
                thread::sleep(byte_delay);
            }
        } else {
            serial.write_all(&line)?;
            serial.flush()?;
        }
        if options.sync {
            wait_for_echo(serial, tty, &line).map_err(|error| {
                io::Error::new(error.kind(), format!("line {line_number}: {error}"))
            })?;
        }
        if let Some(delay) = options.delay {
            thread::sleep(delay);
        }
    }

    if options.verbose {
        eprintln!(
            "upload: completed {line_number} lines from {}",
            path.display()
        );
    }
    Ok(())
}

fn run(options: &Options) -> io::Result<()> {
    let mut serial = OpenOptions::new()
        .read(true)
        .write(true)
        .open(&options.device)?;
    let mut tty = OpenOptions::new().read(true).write(true).open("/dev/tty")?;
    let _serial_guard = TermiosGuard::serial(serial.as_raw_fd())?;
    let _tty_guard = TermiosGuard::terminal(tty.as_raw_fd())?;

    if options.verbose {
        eprintln!(
            "serial: {} at 921600 8N1 with RTS/CTS; delay={}; byte-delay={}; sync={}",
            options.device,
            options
                .delay
                .map(|value| format!("{}ms", value.as_millis()))
                .unwrap_or_else(|| "off".into()),
            options
                .byte_delay
                .map(|value| format!("{}us", value.as_micros()))
                .unwrap_or_else(|| "off".into()),
            options.sync
        );
    }

    let mut serial_buffer = [0_u8; 1024];
    let mut tty_buffer = [0_u8; 256];
    loop {
        let mut descriptors = [
            libc::pollfd {
                fd: serial.as_raw_fd(),
                events: libc::POLLIN,
                revents: 0,
            },
            libc::pollfd {
                fd: tty.as_raw_fd(),
                events: libc::POLLIN,
                revents: 0,
            },
        ];
        // SAFETY: descriptors is a valid two-element pollfd array.
        let ready = unsafe { libc::poll(descriptors.as_mut_ptr(), 2, -1) };
        if ready == -1 {
            let error = io::Error::last_os_error();
            if error.kind() == io::ErrorKind::Interrupted {
                continue;
            }
            return Err(error);
        }

        if descriptors[0].revents & libc::POLLIN != 0 {
            let count = serial.read(&mut serial_buffer)?;
            if count == 0 {
                return Err(io::Error::new(
                    io::ErrorKind::UnexpectedEof,
                    "serial device disconnected",
                ));
            }
            tty.write_all(&serial_buffer[..count])?;
            tty.flush()?;
        }

        if descriptors[1].revents & libc::POLLIN != 0 {
            let count = tty.read(&mut tty_buffer)?;
            if count == 0 {
                return Ok(());
            }
            for &byte in &tty_buffer[..count] {
                match byte {
                    0x03 => return Ok(()),
                    0x12 => {
                        let Some(name) = prompt_for_file(&mut tty)? else {
                            return Ok(());
                        };
                        if name.is_empty() {
                            continue;
                        }
                        if let Err(error) = upload(Path::new(&name), &mut serial, &mut tty, options)
                        {
                            tty.write_all(format!("\r\nupload failed: {error}\r\n").as_bytes())?;
                            tty.flush()?;
                            if options.sync {
                                return Err(error);
                            }
                        }
                    }
                    _ => {
                        serial.write_all(&[byte])?;
                        serial.flush()?;
                    }
                }
            }
        }
    }
}

fn main() -> ExitCode {
    let program = env::args().next().unwrap_or_else(|| "te-rs".into());
    let options = match parse_args(env::args()) {
        Ok(ParseResult::Help) => {
            print!("{}", help(&program));
            return ExitCode::SUCCESS;
        }
        Ok(ParseResult::Run(options)) => options,
        Err(error) => {
            eprintln!("error: {error}\n\n{}", help(&program));
            return ExitCode::from(2);
        }
    };

    match run(&options) {
        Ok(()) => ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("{}: {error}", options.device);
            ExitCode::FAILURE
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn args(values: &[&str]) -> Vec<String> {
        values.iter().map(|value| (*value).into()).collect()
    }

    #[test]
    fn defaults_match_te() {
        let ParseResult::Run(options) = parse_args(args(&["te-rs"])).unwrap() else {
            panic!("expected runnable options");
        };
        assert_eq!(
            options,
            Options {
                device: DEFAULT_DEVICE.into(),
                delay: None,
                byte_delay: None,
                sync: false,
                verbose: false,
            }
        );
    }

    #[test]
    fn parses_all_options_and_device() {
        let ParseResult::Run(options) = parse_args(args(&[
            "te-rs",
            "--verbose",
            "--sync",
            "--delay",
            "7",
            "--byte-delay",
            "100",
            "/dev/ttyACM0",
        ]))
        .unwrap() else {
            panic!("expected runnable options");
        };
        assert_eq!(options.device, "/dev/ttyACM0");
        assert_eq!(options.delay, Some(Duration::from_millis(7)));
        assert_eq!(options.byte_delay, Some(Duration::from_micros(100)));
        assert!(options.sync);
        assert!(options.verbose);
    }

    #[test]
    fn rejects_delay_outside_range() {
        assert!(parse_args(args(&["te-rs", "-d", "0"])).is_err());
        assert!(parse_args(args(&["te-rs", "-d", "11"])).is_err());
        assert!(parse_args(args(&["te-rs", "-b", "0"])).is_err());
        assert!(parse_args(args(&["te-rs", "-b", "10001"])).is_err());
    }

    #[test]
    fn recognizes_placeholder_monitor_errors_case_insensitively() {
        assert_eq!(
            monitor_error(b"Error: INVALID hex code\r\n"),
            Some("invalid")
        );
        assert_eq!(
            monitor_error(b"Load line is TOO LONG\r\n"),
            Some("too long")
        );
    }

    #[test]
    fn permits_normal_monitor_output() {
        assert_eq!(monitor_error(b"L000000...\r\n"), None);
        assert_eq!(monitor_error(b"; comment\r\n"), None);
        assert_eq!(monitor_error(b"G000015\r\nJump...\r\n"), None);
    }

    #[test]
    fn terminal_output_translates_lf_to_crlf() {
        // SAFETY: cfmakeraw accepts a termios structure, and this test only
        // inspects the output flags set by terminal_attributes.
        let attributes = unsafe { std::mem::zeroed() };
        let configured = terminal_attributes(attributes);
        assert_ne!(configured.c_oflag & libc::OPOST, 0);
        assert_ne!(configured.c_oflag & libc::ONLCR, 0);
    }
}
