import { useState } from "react";
import reactLogo from "./assets/react.svg";
import { invoke } from "@tauri-apps/api/core";
import "./App.css";

// Define interface to match Rust PortInfo struct
interface PortInfo {
  name: string;
  port_type: string;
  manufacturer?: string;
  product?: string;
  serial_number?: string;
}

function App() {
  const [greetMsg, setGreetMsg] = useState("");
  const [name, setName] = useState("");
  const [serialPorts, setSerialPorts] = useState<PortInfo[]>([]);
  const [loading, setLoading] = useState(false);

  async function greet() {
    // Learn more about Tauri commands at https://tauri.app/develop/calling-rust/
    setGreetMsg(await invoke("greet", { name }));
  }

  async function fetchSerialPorts() {
    try {
      setLoading(true);
      // Call our Rust function
      const ports = await invoke<PortInfo[]>("list_serial_ports");
      setSerialPorts(ports);
    } catch (error) {
      console.error("Failed to fetch serial ports:", error);
    } finally {
      setLoading(false);
    }
  }

  return (
    <main className="container">
      <h1>FaultyCat Tools</h1>

      <div className="row">
        <a href="https://vitejs.dev" target="_blank">
          <img src="/vite.svg" className="logo vite" alt="Vite logo" />
        </a>
        <a href="https://tauri.app" target="_blank">
          <img src="/tauri.svg" className="logo tauri" alt="Tauri logo" />
        </a>
        <a href="https://reactjs.org" target="_blank">
          <img src={reactLogo} className="logo react" alt="React logo" />
        </a>
      </div>

      <div className="card">
        <h2>Serial Ports</h2>
        <button onClick={fetchSerialPorts} disabled={loading}>
          {loading ? "Loading..." : "List Serial Ports"}
        </button>
        
        {serialPorts.length > 0 ? (
          <div className="serial-ports">
            <table>
              <thead>
                <tr>
                  <th>Name</th>
                  <th>Type</th>
                  <th>Manufacturer</th>
                  <th>Product</th>
                  <th>Serial Number</th>
                </tr>
              </thead>
              <tbody>
                {serialPorts.map((port, index) => (
                  <tr key={index}>
                    <td>{port.name}</td>
                    <td>{port.port_type}</td>
                    <td>{port.manufacturer || "N/A"}</td>
                    <td>{port.product || "N/A"}</td>
                    <td>{port.serial_number || "N/A"}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        ) : (
          <p>No serial ports found or click the button to list them.</p>
        )}
      </div>

      <div className="card">
        <h2>Greeting Example</h2>
        <form
          className="row"
          onSubmit={(e) => {
            e.preventDefault();
            greet();
          }}
        >
          <input
            id="greet-input"
            onChange={(e) => setName(e.currentTarget.value)}
            placeholder="Enter a name..."
          />
          <button type="submit">Greet</button>
        </form>
        <p>{greetMsg}</p>
      </div>
    </main>
  );
}

export default App;