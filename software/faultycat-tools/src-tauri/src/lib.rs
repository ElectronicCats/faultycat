// Learn more about Tauri commands at https://tauri.app/develop/calling-rust/
use serde::{Deserialize, Serialize};
use std::sync::Mutex;
use std::time::Duration;
use std::io::{self, Read};
use std::collections::VecDeque;
use std::time::Instant;
use std::thread;
use std::sync::{Arc, Mutex as StdMutex};

// Struct to manage the serial connection with response buffer
struct SerialConnection {
    port: Option<Box<dyn serialport::SerialPort>>,
    response_buffer: VecDeque<String>,
}

impl SerialConnection {
    fn new() -> Self {
        SerialConnection { 
            port: None,
            response_buffer: VecDeque::with_capacity(100), // Keep last 100 messages
        }
    }

    fn add_response(&mut self, response: String) {
        // Add response to buffer, keeping it under capacity
        if !response.trim().is_empty() {
            self.response_buffer.push_back(response);
            if self.response_buffer.len() > 100 {
                self.response_buffer.pop_front();
            }
        }
    }
}

#[derive(Debug, Serialize, Deserialize)]
pub struct PortInfo {
    name: String,
    port_type: String,
    manufacturer: Option<String>,
    product: Option<String>,
    serial_number: Option<String>,
}

#[tauri::command]
fn list_serial_ports() -> Result<Vec<PortInfo>, String> {
    let ports = serialport::available_ports().map_err(|e| e.to_string())?;

    let port_infos = ports
        .into_iter()
        .map(|port| {
            let port_type = match &port.port_type {
                serialport::SerialPortType::UsbPort(info) => PortInfo {
                    name: port.port_name,
                    port_type: "USB".to_string(),
                    manufacturer: info.manufacturer.clone(),
                    product: info.product.clone(),
                    serial_number: info.serial_number.clone(),
                },
                serialport::SerialPortType::BluetoothPort => PortInfo {
                    name: port.port_name,
                    port_type: "Bluetooth".to_string(),
                    manufacturer: None,
                    product: None,
                    serial_number: None,
                },
                serialport::SerialPortType::PciPort => PortInfo {
                    name: port.port_name,
                    port_type: "PCI".to_string(),
                    manufacturer: None,
                    product: None,
                    serial_number: None,
                },
                _ => PortInfo {
                    name: port.port_name,
                    port_type: "Unknown".to_string(),
                    manufacturer: None,
                    product: None,
                    serial_number: None,
                },
            };

            port_type
        })
        .collect();

    Ok(port_infos)
}

#[tauri::command]
fn connect_serial(
    state: tauri::State<'_, Mutex<SerialConnection>>,
    port_name: &str,
) -> Result<String, String> {
    let mut connection = state.lock().map_err(|e| e.to_string())?;

    // Close any existing connection
    connection.port = None;
    connection.response_buffer.clear();

    // Open a new connection
    match serialport::new(port_name, 115_200) // FaultyCat uses 115200 baud
        .timeout(Duration::from_millis(1000))
        .open()
    {
        Ok(port) => {
            connection.port = Some(port);
            Ok(format!("Connected to {}", port_name))
        }
        Err(e) => Err(format!("Failed to connect: {}", e)),
    }
}

#[tauri::command]
fn disconnect_serial(state: tauri::State<'_, Mutex<SerialConnection>>) -> Result<String, String> {
    let mut connection = state.lock().map_err(|e| e.to_string())?;
    connection.port = None;
    connection.response_buffer.clear();
    Ok("Disconnected".to_string())
}

#[tauri::command]
fn send_command(
    state: tauri::State<'_, Mutex<SerialConnection>>,
    command: &str,
) -> Result<String, String> {
    let mut connection = state.lock().map_err(|e| e.to_string())?;

    match &mut connection.port {
        Some(port) => {
            // Use carriage return (\r) instead of newline (\n)
            // FaultyCat firmware expects \r termination
            let cmd = format!("{}\r", command);
            match port.write(cmd.as_bytes()) {
                Ok(_) => {
                    println!("Sent command: {}", command);
                    Ok(format!("Sent command: {}", command))
                },
                Err(e) => Err(format!("Failed to send command: {}", e)),
            }
        }
        None => Err("Not connected to any port".to_string()),
    }
}

#[tauri::command]
fn read_serial_response(state: tauri::State<'_, Mutex<SerialConnection>>) -> Result<Vec<String>, String> {
    let mut connection = state.lock().map_err(|e| e.to_string())?;
    
    // Check if port is connected
    if let Some(port) = &mut connection.port {
        // Buffer for reading data
        let mut buffer = [0; 1024];
        
        // Try to read from the port
        match port.read(&mut buffer) {
            Ok(bytes_read) if bytes_read > 0 => {
                // Convert the buffer to a string
                if let Ok(data) = String::from_utf8(buffer[..bytes_read].to_vec()) {
                    // We need to handle potentially partial lines and echoed commands
                    for line in data.lines() {
                        // Skip empty lines and filter out echoed commands
                        // (this is a simple heuristic - we may need more complex filtering)
                        let trimmed = line.trim();
                        if !trimmed.is_empty() && !trimmed.starts_with('>') {
                            connection.add_response(line.to_string());
                        }
                    }
                }
            }
            Err(ref e) if e.kind() == io::ErrorKind::TimedOut => {
                // Timeout is normal, just continue
            }
            Err(e) => {
                return Err(format!("Error reading from port: {}", e));
            }
            _ => {}
        }
    }
    
    // Return the current buffer
    Ok(connection.response_buffer.iter().cloned().collect())
}

#[tauri::command]
fn send_command_with_read(
    state: tauri::State<'_, Mutex<SerialConnection>>,
    command: &str, 
    read_duration_ms: u64
) -> Result<String, String> {
    // First send the command
    let _result = send_command(state.clone(), command)?;
    
    // Create a thread-safe copy of the port and a separate response buffer
    let port_mutex = Arc::new(StdMutex::new(None));
    let response_buffer = Arc::new(StdMutex::new(Vec::new()));
    
    // Get the port from the state and store it in our thread-safe container
    {
        let mut connection = state.lock().map_err(|e| e.to_string())?;
        if let Some(port) = connection.port.take() {
            *port_mutex.lock().unwrap() = Some(port);
        } else {
            return Err("Not connected to any port".to_string());
        }
    }
    
    // Clone for the thread
    let port_for_thread = Arc::clone(&port_mutex);
    let responses_for_thread = Arc::clone(&response_buffer);
    
    // Use a channel to signal when the reading is complete
    let (tx, rx) = std::sync::mpsc::channel();
    
    // Spawn thread to read for specified duration
    thread::spawn(move || {
        let start_time = Instant::now();
        let duration = Duration::from_millis(read_duration_ms);
        let mut combined_data = String::new();
        
        while start_time.elapsed() < duration {
            // Read from port
            if let Some(port) = &mut *port_for_thread.lock().unwrap() {
                let mut buffer = [0; 1024];
                match port.read(&mut buffer) {
                    Ok(bytes_read) if bytes_read > 0 => {
                        if let Ok(data) = String::from_utf8(buffer[..bytes_read].to_vec()) {
                            println!("Read data: {}", data);
                            
                            // Add to temporary response buffer
                            if !data.trim().is_empty() {
                                combined_data.push_str(&data);
                                responses_for_thread.lock().unwrap().push(data);
                            }
                        }
                    },
                    Err(ref e) if e.kind() == io::ErrorKind::TimedOut => {
                        // Timeout is normal, just continue
                    },
                    Err(e) => {
                        eprintln!("Error reading from port: {}", e);
                        break;
                    },
                    _ => {}
                }
            }
            
            // Sleep a shorter time to be more responsive
            thread::sleep(Duration::from_millis(10));
        }
        
        println!("Finished reading after {}ms", read_duration_ms);
        let _ = tx.send(());
    });
    
    // Wait for the reading thread to finish, with a timeout for safety
    let _ = rx.recv_timeout(Duration::from_millis(read_duration_ms + 100));
    
    // Collect the responses and update the state
    let collected_data = {
        let responses = response_buffer.lock().unwrap();
        let mut connection = state.lock().map_err(|e| e.to_string())?;
        
        // Build a single string from all responses
        let mut result = String::new();
        for response in responses.iter() {
            connection.add_response(response.clone());
            result.push_str(response);
        }
        
        // Return the port to the state
        if let Some(port) = port_mutex.lock().unwrap().take() {
            connection.port = Some(port);
        }
        
        result
    };
    
    // Return the collected data
    if collected_data.is_empty() {
        Ok("No response received".to_string())
    } else {
        Ok(collected_data)
    }
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {   
    tauri::Builder::default()
        .plugin(tauri_plugin_opener::init())
        .manage(Mutex::new(SerialConnection::new()))
        .invoke_handler(tauri::generate_handler![
            list_serial_ports,
            connect_serial,
            disconnect_serial,
            send_command,
            read_serial_response,
            send_command_with_read
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}