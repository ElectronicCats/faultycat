import { useState, useMemo } from "react";
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
  SelectChangeEvent
} from "@mui/material";
import RefreshIcon from "@mui/icons-material/Refresh";

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
  const [selectedPort, setSelectedPort] = useState<PortInfo | null>(null);
  const prefersDarkMode = useMediaQuery('(prefers-color-scheme: dark)');
  
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
    } finally {
      setLoading(false);
    }
  }
  
  const handlePortChange = (event: SelectChangeEvent) => {
    const portName = event.target.value;
    const port = serialPorts.find(p => p.name === portName) || null;
    setSelectedPort(port);
  };

  return (
    <ThemeProvider theme={theme}>
      <CssBaseline />
      <Container maxWidth="lg" sx={{ py: 4 }}>
        <Typography variant="h3" component="h1" gutterBottom align="center">
          FaultyCat Tools
        </Typography>

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
                >
                  {serialPorts.map((port, index) => (
                    <MenuItem key={index} value={port.name}>
                      {port.name} - {port.manufacturer || "N/A"} {port.product ? `(${port.product})` : ""}
                    </MenuItem>
                  ))}
                </Select>
              </FormControl>
              
              {selectedPort && (
                <Box sx={{ mt: 2, p: 2, border: '1px solid', borderColor: 'divider', borderRadius: 1 }}>
                  <Typography variant="h6" gutterBottom>Selected Port Details:</Typography>
                  <Typography><strong>Name:</strong> {selectedPort.name}</Typography>
                  <Typography><strong>Type:</strong> {selectedPort.port_type}</Typography>
                  <Typography><strong>Manufacturer:</strong> {selectedPort.manufacturer || "N/A"}</Typography>
                  <Typography><strong>Product:</strong> {selectedPort.product || "N/A"}</Typography>
                  <Typography><strong>Serial Number:</strong> {selectedPort.serial_number || "N/A"}</Typography>
                </Box>
              )}
            </Box>
          ) : (
            <Typography variant="body1" color="text.secondary" sx={{ mt: 2 }}>
              No serial ports found or click the button to list them.
            </Typography>
          )}
        </Paper>
      </Container>
    </ThemeProvider>
  );
}

export default App;