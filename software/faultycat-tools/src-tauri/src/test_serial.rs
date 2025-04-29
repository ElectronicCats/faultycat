use serialport;
use std::io::{self, Read, Write};
use std::time::{Duration, Instant};
use std::thread;

pub fn run_serial_test() {
    println!("Starting serial test on /dev/cu.usbmodem1301...");
    
    // Open the serial port
    let mut port = match serialport::new("/dev/cu.usbmodem1301", 115_200)
        .timeout(Duration::from_millis(1000))
        .open() {
            Ok(port) => port,
            Err(e) => {
                eprintln!("Failed to open port: {}", e);
                return;
            }
        };
    
    println!("Connected to /dev/cu.usbmodem1301");
    
    // Start time for the 5-second timer
    let mut last_send_time = Instant::now();
    
    // Buffer for reading data
    let mut buffer = [0; 1024];
    
    // Main loop
    loop {
        // Check if it's time to send "h" (every 5 seconds)
        if last_send_time.elapsed() >= Duration::from_secs(5) {
            println!("Sending 'h' to device...");
            match port.write("a\r".as_bytes()) {
                Ok(_) => println!("Sent 'h' to device"),
                Err(e) => eprintln!("Failed to send 'h': {}", e),
            };
            last_send_time = Instant::now();
        }
        
        // Try to read data from the port
        match port.read(&mut buffer) {
            Ok(bytes_read) if bytes_read > 0 => {
                // Convert the buffer to a string and print it
                if let Ok(data) = String::from_utf8(buffer[..bytes_read].to_vec()) {
                    print!("Received: {}", data);
                } else {
                    eprintln!("Received non-UTF8 data");
                }
            },
            Err(ref e) if e.kind() == io::ErrorKind::TimedOut => {
                // Timeout is normal, just continue
            },
            Err(e) => {
                eprintln!("Error reading from port: {}", e);
            },
            _ => {}
        }
        
        // Short sleep to prevent CPU hogging
        thread::sleep(Duration::from_millis(10));
    }
}