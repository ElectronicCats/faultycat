// Learn more about Tauri commands at https://tauri.app/develop/calling-rust/
use serde::{Serialize, Deserialize};
use serialport::SerialPortInfo;

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
    
    let port_infos = ports.into_iter().map(|port| {
        let port_type = match &port.port_type {
            serialport::SerialPortType::UsbPort(info) => {
                PortInfo {
                    name: port.port_name,
                    port_type: "USB".to_string(),
                    manufacturer: info.manufacturer.clone(),
                    product: info.product.clone(),
                    serial_number: info.serial_number.clone(),
                }
            },
            serialport::SerialPortType::BluetoothPort => {
                PortInfo {
                    name: port.port_name,
                    port_type: "Bluetooth".to_string(),
                    manufacturer: None,
                    product: None,
                    serial_number: None,
                }
            },
            serialport::SerialPortType::PciPort => {
                PortInfo {
                    name: port.port_name,
                    port_type: "PCI".to_string(),
                    manufacturer: None,
                    product: None,
                    serial_number: None,
                }
            },
            _ => {
                PortInfo {
                    name: port.port_name,
                    port_type: "Unknown".to_string(),
                    manufacturer: None,
                    product: None,
                    serial_number: None,
                }
            }
        };
        
        port_type
    }).collect();
    
    Ok(port_infos)
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_opener::init())
        .invoke_handler(tauri::generate_handler![greet, list_serial_ports])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}