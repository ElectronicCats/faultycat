import { useState, useMemo, useEffect } from "react";
import { invoke } from "@tauri-apps/api/core";
import "./App.css";
import {
  Container,
  Typography,
  Button,
  Paper,
  Box,
  CircularProgress,
  ThemeProvider,
  createTheme,
  useMediaQuery,
  CssBaseline,
  Select,
  MenuItem,
  FormControl,
  InputLabel,
  SelectChangeEvent,
  TextField,
  Alert,
  Snackbar,
  Grid
} from "@mui/material";
import RefreshIcon from "@mui/icons-material/Refresh";
import SendIcon from "@mui/icons-material/Send";
import UsbIcon from "@mui/icons-material/Usb";
import UsbOffIcon from "@mui/icons-material/UsbOff";

// Define interface to match Rust PortInfo struct
interface PortInfo {
  name: string;
  port_type: string;
  manufacturer?: string;
  product?: string;
  serial_number?: string;
}

function App() {
  const [serialPorts, setSerialPorts] = useState<PortInfo[]>([]);
  const [loading, setLoading] = useState(false);
  const [connecting, setConnecting] = useState(false);
  const [selectedPort, setSelectedPort] = useState<PortInfo | null>(null);
  const [connected, setConnected] = useState(false);
  const [command, setCommand] = useState("");
  const [sendingCommand, setSendingCommand] = useState(false);
  const [message, setMessage] = useState<{text: string, severity: 'success' | 'error' | 'info' | 'warning'} | null>(null);
  const prefersDarkMode = useMediaQuery('(prefers-color-scheme: dark)');

  useEffect(() => {
    fetchSerialPorts();
  }, []);
  
  const theme = useMemo(
    () =>
      createTheme({
        palette: {
          mode: prefersDarkMode ? 'dark' : 'light',
        },
      }),
    [prefersDarkMode],
  );

  async function fetchSerialPorts() {
    try {
      setLoading(true);
      // Call our Rust function
      const ports = await invoke<PortInfo[]>("list_serial_ports");
      // Filter for only USB ports
      const usbPorts = ports.filter(port => 
        port.port_type.toLowerCase().includes('usb')
      );
      setSerialPorts(usbPorts);
      // Reset selection when refreshing ports
      setSelectedPort(null);
    } catch (error) {
      console.error("Failed to fetch serial ports:", error);
      setMessage({text: `Error: ${error}`, severity: 'error'});
    } finally {
      setLoading(false);
    }
  }
  
  const handlePortChange = (event: SelectChangeEvent) => {
    const portName = event.target.value;
    const port = serialPorts.find(p => p.name === portName) || null;
    setSelectedPort(port);
  };

  const handleConnect = async () => {
    if (!selectedPort) return;
    
    try {
      setConnecting(true);
      const result = await invoke<string>("connect_serial", { 
        portName: selectedPort.name 
      });
      setConnected(true);
      setMessage({text: result, severity: 'success'});
    } catch (error) {
      console.error("Connection error:", error);
      setMessage({text: `Connection error: ${error}`, severity: 'error'});
    } finally {
      setConnecting(false);
    }
  };

  const handleDisconnect = async () => {
    try {
      setConnecting(true);
      const result = await invoke<string>("disconnect_serial");
      setConnected(false);
      setMessage({text: result, severity: 'success'});
    } catch (error) {
      console.error("Disconnection error:", error);
      setMessage({text: `Disconnection error: ${error}`, severity: 'error'});
    } finally {
      setConnecting(false);
    }
  };

  const handleSendCommand = async () => {
    if (!connected || !command) return;
    
    try {
      setSendingCommand(true);
      const result = await invoke<string>("send_command", { command });
      setMessage({text: result, severity: 'success'});
    } catch (error) {
      console.error("Command error:", error);
      setMessage({text: `Command error: ${error}`, severity: 'error'});
    } finally {
      setSendingCommand(false);
    }
  };

  // Handle common FaultyCat commands
  const sendQuickCommand = async (cmd: string) => {
    if (!connected) return;
    
    try {
      setSendingCommand(true);
      const result = await invoke<string>("send_command", { command: cmd });
      setMessage({text: result, severity: 'success'});
    } catch (error) {
      console.error("Command error:", error);
      setMessage({text: `Command error: ${error}`, severity: 'error'});
    } finally {
      setSendingCommand(false);
    }
  };

  return (
    <ThemeProvider theme={theme}>
      <CssBaseline />
      <Container maxWidth="lg" sx={{ py: 4 }}>
        <Typography variant="h3" component="h1" gutterBottom align="center">
          FaultyCat Tools
        </Typography>

        {message && (
          <Snackbar 
            open={!!message} 
            autoHideDuration={6000} 
            onClose={() => setMessage(null)}
          >
            <Alert severity={message.severity} onClose={() => setMessage(null)}>
              {message.text}
            </Alert>
          </Snackbar>
        )}

        <Paper elevation={3} sx={{ p: 3, mb: 4 }}>
          <Box sx={{ display: 'flex', alignItems: 'center', mb: 2, justifyContent: 'space-between' }}>
            <Typography variant="h5" component="h2">
              Serial Ports
            </Typography>
            <Button
              variant="contained"
              color="primary"
              onClick={fetchSerialPorts}
              disabled={loading}
              startIcon={loading ? <CircularProgress size={20} color="inherit" /> : <RefreshIcon />}
            >
              {loading ? "Loading..." : "List Serial Ports"}
            </Button>
          </Box>

          {serialPorts.length > 0 ? (
            <Box sx={{ mt: 2 }}>
              <FormControl fullWidth sx={{ mb: 3 }}>
                <InputLabel id="port-select-label">Select Serial Port</InputLabel>
                <Select
                  labelId="port-select-label"
                  id="port-select"
                  value={selectedPort?.name || ''}
                  label="Select Serial Port"
                  onChange={handlePortChange}
                  disabled={connected}
                >
                  {serialPorts.map((port, index) => (
                    <MenuItem key={index} value={port.name}>
                      {port.name} - {port.manufacturer || "N/A"} {port.product ? `(${port.product})` : ""}
                    </MenuItem>
                  ))}
                </Select>
              </FormControl>
              
              {selectedPort && (
                <>
                  <Box sx={{ mt: 2, p: 2, border: '1px solid', borderColor: 'divider', borderRadius: 1 }}>
                    <Typography variant="h6" gutterBottom>Selected Port Details:</Typography>
                    <Typography><strong>Name:</strong> {selectedPort.name}</Typography>
                    <Typography><strong>Type:</strong> {selectedPort.port_type}</Typography>
                    <Typography><strong>Manufacturer:</strong> {selectedPort.manufacturer || "N/A"}</Typography>
                    <Typography><strong>Product:</strong> {selectedPort.product || "N/A"}</Typography>
                    <Typography><strong>Serial Number:</strong> {selectedPort.serial_number || "N/A"}</Typography>
                  </Box>
                  
                  <Box sx={{ mt: 2, display: 'flex', justifyContent: 'center' }}>
                    {!connected ? (
                      <Button
                        variant="contained"
                        color="success"
                        onClick={handleConnect}
                        disabled={connecting}
                        startIcon={connecting ? <CircularProgress size={20} color="inherit" /> : <UsbIcon />}
                      >
                        {connecting ? "Connecting..." : "Connect"}
                      </Button>
                    ) : (
                      <Button
                        variant="contained"
                        color="error"
                        onClick={handleDisconnect}
                        disabled={connecting}
                        startIcon={connecting ? <CircularProgress size={20} color="inherit" /> : <UsbOffIcon />}
                      >
                        {connecting ? "Disconnecting..." : "Disconnect"}
                      </Button>
                    )}
                  </Box>
                </>
              )}
            </Box>
          ) : (
            <Typography variant="body1" color="text.secondary" sx={{ mt: 2 }}>
              No serial ports found or click the button to list them.
            </Typography>
          )}
        </Paper>

        {connected && (
          <Paper elevation={3} sx={{ p: 3 }}>
            <Typography variant="h5" component="h2" gutterBottom>
              FaultyCat Commands
            </Typography>

            <Typography variant="body1" gutterBottom>
              Send a command to the FaultyCat device:
            </Typography>

            <Box sx={{ mb: 3 }}>
              <Grid container spacing={2}>
                <Grid item xs={12} sm={8}>
                  <TextField
                    fullWidth
                    label="Command"
                    value={command}
                    onChange={(e) => setCommand(e.target.value)}
                    helperText="Enter a command to send"
                  />
                </Grid>
                <Grid item xs={12} sm={4}>
                  <Button
                    fullWidth
                    variant="contained"
                    color="primary"
                    onClick={handleSendCommand}
                    disabled={sendingCommand || !command}
                    startIcon={sendingCommand ? <CircularProgress size={20} color="inherit" /> : <SendIcon />}
                    sx={{ height: '56px' }}
                  >
                    {sendingCommand ? "Sending..." : "Send Command"}
                  </Button>
                </Grid>
              </Grid>
            </Box>

            <Typography variant="h6" gutterBottom>
              Quick Commands:
            </Typography>
            
            <Box sx={{ display: 'flex', flexWrap: 'wrap', gap: 2 }}>
              <Button variant="outlined" onClick={() => sendQuickCommand("a")}>
                Arm (a)
              </Button>
              <Button variant="outlined" onClick={() => sendQuickCommand("d")}>
                Disarm (d)
              </Button>
              <Button variant="outlined" onClick={() => sendQuickCommand("p")}>
                Pulse (p)
              </Button>
              <Button variant="outlined" onClick={() => sendQuickCommand("s")}>
                Status (s)
              </Button>
              <Button variant="outlined" onClick={() => sendQuickCommand("h")}>
                Help (h)
              </Button>
              <Button variant="outlined" onClick={() => sendQuickCommand("g")}>
                Glitch (g)
              </Button>
              <Button variant="outlined" onClick={() => sendQuickCommand("j")}>
                JTAG Scan (j)
              </Button>
              <Button variant="outlined" onClick={() => sendQuickCommand("c")}>
                Configure (c)
              </Button>
            </Box>
          </Paper>
        )}
      </Container>
    </ThemeProvider>
  );
}

export default App;