import { useState } from "react";
import { invoke } from "@tauri-apps/api/core";
import { 
  Container, 
  Typography, 
  Button, 
  Paper, 
  Table,
  TableBody,
  TableCell,
  TableContainer,
  TableHead,
  TableRow,
  Box,
  CircularProgress
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
          <TableContainer component={Paper} elevation={1} sx={{ mt: 2 }}>
            <Table aria-label="serial ports table">
              <TableHead>
                <TableRow sx={{ backgroundColor: 'primary.light' }}>
                  <TableCell>Name</TableCell>
                  <TableCell>Type</TableCell>
                  <TableCell>Manufacturer</TableCell>
                  <TableCell>Product</TableCell>
                  <TableCell>Serial Number</TableCell>
                </TableRow>
              </TableHead>
              <TableBody>
                {serialPorts.map((port, index) => (
                  <TableRow key={index} sx={{ '&:nth-of-type(odd)': { backgroundColor: 'action.hover' } }}>
                    <TableCell>{port.name}</TableCell>
                    <TableCell>{port.port_type}</TableCell>
                    <TableCell>{port.manufacturer || "N/A"}</TableCell>
                    <TableCell>{port.product || "N/A"}</TableCell>
                    <TableCell>{port.serial_number || "N/A"}</TableCell>
                  </TableRow>
                ))}
              </TableBody>
            </Table>
          </TableContainer>
        ) : (
          <Typography variant="body1" color="text.secondary" sx={{ mt: 2 }}>
            No serial ports found or click the button to list them.
          </Typography>
        )}
      </Paper>
    </Container>
  );
}

export default App;