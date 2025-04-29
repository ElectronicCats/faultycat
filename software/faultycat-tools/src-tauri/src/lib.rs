// Learn more about Tauri commands at https://tauri.app/develop/calling-rust/
use serde::{Deserialize, Serialize};
use serialport::SerialPortInfo;
use std::sync::Mutex;
use std::time::Duration;

// Struct to manage the serial connection
struct SerialConnection {
    port: Option<Box<dyn serialport::SerialPort>>,
}

impl SerialConnection {
    fn new() -> Self {
        SerialConnection { port: None }
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
fn greet(name: &str) -> String {
    format!("Hello, {}! You've been greeted from Rust!", name)
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

    // Open a new connection
    match serialport::new(port_name, 115200) // FaultyCat uses 921600 baud
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
            // Add a newline to the command (FaultyCat expects commands followed by newline)
            let cmd = format!("{}\n", command);
            match port.write(cmd.as_bytes()) {
                Ok(_) => Ok(format!("Sent command: {}", command)),
                Err(e) => Err(format!("Failed to send command: {}", e)),
            }
        }
        None => Err("Not connected to any port".to_string()),
    }
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_opener::init())
        .manage(Mutex::new(SerialConnection::new()))
        .invoke_handler(tauri::generate_handler![
            greet,
            list_serial_ports,
            connect_serial,
            disconnect_serial,
            send_command
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
