// Learn more about Tauri commands at https://tauri.app/develop/calling-rust/
use serde::{Deserialize, Serialize};
use std::sync::Mutex;
use std::time::Duration;
use std::io::{self, Read};
use std::collections::VecDeque;

mod test_serial;

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

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    // test_serial::run_serial_test();
    
    tauri::Builder::default()
        .plugin(tauri_plugin_opener::init())
        .manage(Mutex::new(SerialConnection::new()))
        .invoke_handler(tauri::generate_handler![
            list_serial_ports,
            connect_serial,
            disconnect_serial,
            send_command,
            read_serial_response
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}