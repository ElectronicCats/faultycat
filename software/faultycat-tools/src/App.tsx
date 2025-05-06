import { useState, useMemo, useEffect, useRef } from "react";
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
  Grid,
  Divider,
  FormControlLabel,
  Radio,
  RadioGroup,
  FormLabel,
  Slider,
  FormGroup,
  LinearProgress,
  Accordion,
  AccordionSummary,
  AccordionDetails
} from "@mui/material";
import RefreshIcon from "@mui/icons-material/Refresh";
import SendIcon from "@mui/icons-material/Send";
import UsbIcon from "@mui/icons-material/Usb";
import UsbOffIcon from "@mui/icons-material/UsbOff";
import ClearIcon from "@mui/icons-material/Clear";
import ExpandMoreIcon from '@mui/icons-material/ExpandMore';
import TuneIcon from '@mui/icons-material/Tune';

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
  const [message, setMessage] = useState<{ text: string, severity: 'success' | 'error' | 'info' | 'warning' } | null>(null);
  const [responses, setResponses] = useState<string[]>([]);
  const prefersDarkMode = useMediaQuery('(prefers-color-scheme: dark)');
  const responsesContainerRef = useRef<HTMLDivElement>(null);
  const [glitchConfig, setGlitchConfig] = useState({
    triggerType: "3", // Default: Rising Edge
    triggerPull: "0", // Default: None
    glitchOutput: "1", // Default: LP
    delayBeforePulse: 1000, // Default cycle value
    pulseWidth: 2500, // Default cycle value
  });
  const [configuring, setConfiguring] = useState(false);
  const [configResponse, setConfigResponse] = useState("");
  const [adcConfig, setAdcConfig] = useState({
    sampleCount: 1000, // Default sample count
  });
  const [adcConfiguring, setAdcConfiguring] = useState(false);
  const [adcResponse, setAdcResponse] = useState("");

  useEffect(() => {
    fetchSerialPorts();
  }, []);

  // Auto-scroll to the bottom when new responses arrive
  useEffect(() => {
    if (responsesContainerRef.current) {
      responsesContainerRef.current.scrollTop = responsesContainerRef.current.scrollHeight;
    }
  }, [responses]);

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
      setMessage({ text: `Error: ${error}`, severity: 'error' });
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
      setMessage({ text: result, severity: 'success' });

      // Clear old responses
      setResponses([]);
    } catch (error) {
      console.error("Connection error:", error);
      setMessage({ text: `Connection error: ${error}`, severity: 'error' });
    } finally {
      setConnecting(false);
    }
  };

  const handleDisconnect = async () => {
    try {
      setConnecting(true);
      const result = await invoke<string>("disconnect_serial");
      setConnected(false);
      setMessage({ text: result, severity: 'success' });
    } catch (error) {
      console.error("Disconnection error:", error);
      setMessage({ text: `Disconnection error: ${error}`, severity: 'error' });
    } finally {
      setConnecting(false);
    }
  };

  const handleSendCommand = async () => {
    if (!connected || !command) return;

    try {

      setSendingCommand(true);
      const result = await invoke<string>("send_command_with_read", {
        command,
        readDurationMs: 50
      });
      setMessage({ text: result, severity: 'success' });
      // Clear command after sending
      setCommand("");
    } catch (error) {
      console.error("Command error:", error);
      setMessage({ text: `Command error: ${error}`, severity: 'error' });
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
      setMessage({ text: result, severity: 'success' });
    } catch (error) {
      console.error("Command error:", error);
      setMessage({ text: `Command error: ${error}`, severity: 'error' });
    } finally {
      setSendingCommand(false);
    }
  };

  const clearResponses = () => {
    setResponses([]);
  };

  /**
   * Handles changes to the glitch configuration form
   * @param {React.ChangeEvent<HTMLInputElement>} event - The change event
   */
  const handleGlitchConfigChange = (event) => {
    const { name, value } = event.target;
    setGlitchConfig((prev) => ({
      ...prev,
      [name]: value,
    }));
  };

  /**
   * Submits the glitch configuration to the device
   * Uses co command to enter configuration mode and then sends each parameter
   */
  const submitGlitchConfig = async () => {
    if (!connected) return;
  
    try {
      setConfiguring(true);
      setConfigResponse("");
  
      // Send the initial configure glitcher command
      const initialResponse = await invoke<string>("send_command", {
        command: "co"
        // readDurationMs: 1000
      });
  
      let fullResponse = initialResponse;
  
      // Function to send a value after receiving a prompt
      const sendConfigValue = async (value: string, readDuration = 1000) => {
        // Wait longer to ensure device is ready for input
        await new Promise(resolve => setTimeout(resolve, 500));
        
        const response = await invoke<string>("send_command", {
          command: value
          // readDurationMs: readDuration
        });
        
        return response;
      };
  
      // Send each configuration value with longer read durations
      fullResponse += await sendConfigValue(glitchConfig.triggerType, 1000);
      fullResponse += await sendConfigValue(glitchConfig.triggerPull, 1000);
      fullResponse += await sendConfigValue(glitchConfig.glitchOutput, 1000);
      fullResponse += await sendConfigValue(glitchConfig.delayBeforePulse.toString(), 1000);
      fullResponse += await sendConfigValue(glitchConfig.pulseWidth.toString(), 2000);
  
      setConfigResponse(fullResponse);
      setMessage({ text: "Glitch configuration completed successfully!", severity: 'success' });
      
      // Get the current configuration to verify
      await new Promise(resolve => setTimeout(resolve, 1000));
      const configStatus = await invoke<string>("send_command_with_read", {
        command: "gl",
        readDurationMs: 1000
      });
      setConfigResponse(fullResponse + "\n\n" + configStatus);
      
    } catch (error) {
      console.error("Configuration error:", error);
      setMessage({ text: `Configuration error: ${error}`, severity: 'error' });
    } finally {
      setConfiguring(false);
    }
  };

  /**
   * Gets the current glitch configuration from the device
   */
  const getGlitchConfig = async () => {
    if (!connected) return;

    try {
      setConfiguring(true);
      const result = await invoke<string>("send_command_with_read", {
        command: "gl",
        readDurationMs: 500
      });
      setConfigResponse(result);
    } catch (error) {
      console.error("Error getting glitch configuration:", error);
      setMessage({ text: `Error: ${error}`, severity: 'error' });
    } finally {
      setConfiguring(false);
    }
  };

  /**
   * Configures the ADC sample count
   */
  const configureAdc = async () => {
    if (!connected) return;

    try {
      setAdcConfiguring(true);
      setAdcResponse("");

      // Send the configure adc command with the sample count
      const result = await invoke<string>("send_command_with_read", {
        command: "con",
        readDurationMs: 200
      });

      let fullResponse = result;

      // Send the sample count
      const sampleResponse = await invoke<string>("send_command_with_read", {
        command: adcConfig.sampleCount.toString(),
        readDurationMs: 200
      });

      fullResponse += sampleResponse;
      setAdcResponse(fullResponse);
      setMessage({ text: "ADC configuration completed successfully!", severity: 'success' });
    } catch (error) {
      console.error("ADC configuration error:", error);
      setMessage({ text: `ADC configuration error: ${error}`, severity: 'error' });
    } finally {
      setAdcConfiguring(false);
    }
  };

  /**
   * Displays ADC captured data
   */
  const displayAdc = async () => {
    if (!connected) return;

    try {
      setAdcConfiguring(true);
      const result = await invoke<string>("send_command_with_read", {
        command: "di",
        readDurationMs: 1000 // Longer timeout as it might return more data
      });
      setAdcResponse(result);
      setMessage({ text: "ADC data retrieved successfully!", severity: 'success' });
    } catch (error) {
      console.error("Error retrieving ADC data:", error);
      setMessage({ text: `Error: ${error}`, severity: 'error' });
    } finally {
      setAdcConfiguring(false);
    }
  };

  /**
   * Executes the configured glitch
   */
  const executeGlitch = async () => {
    if (!connected) return;

    try {
      setConfiguring(true);
      const result = await invoke<string>("send_command_with_read", {
        command: "g",
        readDurationMs: 500
      });
      setConfigResponse(result);
      setMessage({ text: "Glitch executed successfully!", severity: 'success' });
    } catch (error) {
      console.error("Error executing glitch:", error);
      setMessage({ text: `Error: ${error}`, severity: 'error' });
    } finally {
      setConfiguring(false);
    }
  };

  return (
    <ThemeProvider theme={theme}>
      <CssBaseline />
      <Container
        maxWidth="lg"
        disableGutters
        sx={{
          height: '100vh',
          display: 'flex',
          flexDirection: 'column',
          overflow: 'auto',
          py: 2,
          px: 2
        }}
      >
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
              No serial ports found, please connect your FaultyCat.
            </Typography>
          )}
        </Paper>

        {connected && (
          <>
            <Paper elevation={3} sx={{ p: 3, mt: 4 }}>
              <Accordion>
                <AccordionSummary
                  expandIcon={<ExpandMoreIcon />}
                  aria-controls="panel1a-content"
                  id="panel1a-header"
                >
                  <Box sx={{ display: 'flex', alignItems: 'center' }}>
                    <TuneIcon sx={{ mr: 1 }} />
                    <Typography variant="h5" component="h2">
                      Glitch Configuration
                    </Typography>
                  </Box>
                </AccordionSummary>
                <AccordionDetails>
                  <Grid container spacing={3}>
                    {/* Trigger Type */}
                    <Grid item xs={12} md={6}>
                      <FormControl component="fieldset">
                        <FormLabel component="legend">Trigger Type</FormLabel>
                        <RadioGroup
                          name="triggerType"
                          value={glitchConfig.triggerType}
                          onChange={handleGlitchConfigChange}
                        >
                          <FormControlLabel value="0" control={<Radio />} label="None" />
                          <FormControlLabel value="1" control={<Radio />} label="High" />
                          <FormControlLabel value="2" control={<Radio />} label="Low" />
                          <FormControlLabel value="3" control={<Radio />} label="Rising Edge" />
                          <FormControlLabel value="4" control={<Radio />} label="Falling Edge" />
                          <FormControlLabel value="5" control={<Radio />} label="Pulse Positive" />
                          <FormControlLabel value="6" control={<Radio />} label="Pulse Negative" />
                        </RadioGroup>
                      </FormControl>
                    </Grid>

                    {/* Trigger Pull Configuration */}
                    <Grid item xs={12} md={6}>
                      <FormControl component="fieldset">
                        <FormLabel component="legend">Trigger Pull Configuration</FormLabel>
                        <RadioGroup
                          name="triggerPull"
                          value={glitchConfig.triggerPull}
                          onChange={handleGlitchConfigChange}
                        >
                          <FormControlLabel value="0" control={<Radio />} label="None" />
                          <FormControlLabel value="1" control={<Radio />} label="Pull Up" />
                          <FormControlLabel value="2" control={<Radio />} label="Pull Down" />
                        </RadioGroup>
                      </FormControl>
                    </Grid>

                    {/* Glitch Output */}
                    <Grid item xs={12} md={6}>
                      <FormControl component="fieldset">
                        <FormLabel component="legend">Glitch Output</FormLabel>
                        <RadioGroup
                          name="glitchOutput"
                          value={glitchConfig.glitchOutput}
                          onChange={handleGlitchConfigChange}
                        >
                          <FormControlLabel value="0" control={<Radio />} label="None" />
                          <FormControlLabel value="1" control={<Radio />} label="LP" />
                          <FormControlLabel value="2" control={<Radio />} label="HP" />
                        </RadioGroup>
                      </FormControl>
                    </Grid>

                    {/* Numeric inputs */}
                    <Grid item xs={12} md={6}>
                      <FormControl fullWidth sx={{ mb: 3 }}>
                        <FormLabel>Delay Before Pulse (cycles)</FormLabel>
                        <Box sx={{ display: 'flex', alignItems: 'center' }}>
                          <Slider
                            name="delayBeforePulse"
                            value={glitchConfig.delayBeforePulse}
                            onChange={(e, newValue) => setGlitchConfig(prev => ({ ...prev, delayBeforePulse: newValue }))}
                            min={0}
                            max={10000}
                            step={100}
                            valueLabelDisplay="auto"
                            aria-labelledby="delay-slider"
                            sx={{ mr: 2 }}
                          />
                          <TextField
                            value={glitchConfig.delayBeforePulse}
                            onChange={(e) => {
                              const value = parseInt(e.target.value) || 0;
                              setGlitchConfig(prev => ({ ...prev, delayBeforePulse: value }));
                            }}
                            type="number"
                            margin="dense"
                            size="small"
                            sx={{ width: '100px' }}
                          />
                        </Box>
                      </FormControl>

                      <FormControl fullWidth>
                        <FormLabel>Pulse Width (cycles)</FormLabel>
                        <Box sx={{ display: 'flex', alignItems: 'center' }}>
                          <Slider
                            name="pulseWidth"
                            value={glitchConfig.pulseWidth}
                            onChange={(e, newValue) => setGlitchConfig(prev => ({ ...prev, pulseWidth: newValue }))}
                            min={0}
                            max={10000}
                            step={100}
                            valueLabelDisplay="auto"
                            aria-labelledby="pulse-slider"
                            sx={{ mr: 2 }}
                          />
                          <TextField
                            value={glitchConfig.pulseWidth}
                            onChange={(e) => {
                              const value = parseInt(e.target.value) || 0;
                              setGlitchConfig(prev => ({ ...prev, pulseWidth: value }));
                            }}
                            type="number"
                            margin="dense"
                            size="small"
                            sx={{ width: '100px' }}
                          />
                        </Box>
                      </FormControl>
                    </Grid>

                    {/* Buttons */}
                    <Grid item xs={12}>
                      <Box sx={{ display: 'flex', gap: 2, justifyContent: 'center', mt: 2 }}>
                        <Button
                          variant="contained"
                          color="primary"
                          onClick={submitGlitchConfig}
                          disabled={configuring || !connected}
                          startIcon={configuring ? <CircularProgress size={20} color="inherit" /> : <TuneIcon />}
                        >
                          {configuring ? "Configuring..." : "Apply Configuration"}
                        </Button>

                        <Button
                          variant="outlined"
                          color="secondary"
                          onClick={getGlitchConfig}
                          disabled={configuring || !connected}
                        >
                          Get Current Configuration
                        </Button>
                      </Box>
                    </Grid>
                  </Grid>

                  {/* Response display */}
                  {configuring && (
                    <Box sx={{ width: '100%', mt: 3 }}>
                      <LinearProgress />
                    </Box>
                  )}

                  {configResponse && (
                    <Box sx={{ mt: 3, p: 2, backgroundColor: 'background.paper', borderRadius: 1, border: '1px solid', borderColor: 'divider', maxHeight: '300px', overflowY: 'auto' }}>
                      <Typography variant="h6" gutterBottom>Device Response:</Typography>
                      <pre style={{ whiteSpace: 'pre-wrap', fontFamily: 'monospace' }}>
                        {configResponse}
                      </pre>
                    </Box>
                  )}
                </AccordionDetails>
              </Accordion>

              {/* New ADC Configuration Accordion */}
              <Accordion sx={{ mt: 2 }}>
                <AccordionSummary
                  expandIcon={<ExpandMoreIcon />}
                  aria-controls="panel2a-content"
                  id="panel2a-header"
                >
                  <Box sx={{ display: 'flex', alignItems: 'center' }}>
                    <TuneIcon sx={{ mr: 1 }} />
                    <Typography variant="h5" component="h2">
                      ADC Configuration
                    </Typography>
                  </Box>
                </AccordionSummary>
                <AccordionDetails>
                  <Grid container spacing={3}>
                    {/* ADC Sample Count */}
                    <Grid item xs={12} md={6}>
                      <FormControl fullWidth>
                        <FormLabel>ADC Sample Count (max: 30000)</FormLabel>
                        <Box sx={{ display: 'flex', alignItems: 'center' }}>
                          <Slider
                            value={adcConfig.sampleCount}
                            onChange={(e, newValue) => setAdcConfig(prev => ({ ...prev, sampleCount: newValue as number }))}
                            min={100}
                            max={30000}
                            step={100}
                            valueLabelDisplay="auto"
                            aria-labelledby="adc-sample-slider"
                            sx={{ mr: 2 }}
                          />
                          <TextField
                            value={adcConfig.sampleCount}
                            onChange={(e) => {
                              const value = parseInt(e.target.value) || 100;
                              const clamped = Math.min(30000, Math.max(100, value));
                              setAdcConfig(prev => ({ ...prev, sampleCount: clamped }));
                            }}
                            type="number"
                            margin="dense"
                            size="small"
                            sx={{ width: '100px' }}
                            inputProps={{
                              min: 100,
                              max: 30000
                            }}
                          />
                        </Box>
                      </FormControl>
                    </Grid>

                    {/* Buttons */}
                    <Grid item xs={12} md={6}>
                      <Box sx={{ height: '100%', display: 'flex', flexDirection: 'column', justifyContent: 'center', gap: 2 }}>
                        <Button
                          variant="contained"
                          color="primary"
                          onClick={configureAdc}
                          disabled={adcConfiguring || !connected}
                          fullWidth
                        >
                          Configure ADC Sample Count (con)
                        </Button>

                        <Button
                          variant="outlined"
                          color="secondary"
                          onClick={displayAdc}
                          disabled={adcConfiguring || !connected}
                          fullWidth
                        >
                          Display ADC Data (di)
                        </Button>
                      </Box>
                    </Grid>
                  </Grid>

                  {/* Response display */}
                  {adcConfiguring && (
                    <Box sx={{ width: '100%', mt: 3 }}>
                      <LinearProgress />
                    </Box>
                  )}

                  {adcResponse && (
                    <Box sx={{ mt: 3, p: 2, backgroundColor: 'background.paper', borderRadius: 1, border: '1px solid', borderColor: 'divider', maxHeight: '300px', overflowY: 'auto' }}>
                      <Typography variant="h6" gutterBottom>ADC Response:</Typography>
                      <pre style={{ whiteSpace: 'pre-wrap', fontFamily: 'monospace' }}>
                        {adcResponse}
                      </pre>
                    </Box>
                  )}
                </AccordionDetails>
              </Accordion>

              {/* Add Execute Glitch button outside the accordion for quick access */}
              <Box sx={{ display: 'flex', justifyContent: 'center', mt: 2 }}>
                <Button
                  variant="contained"
                  color="warning"
                  onClick={executeGlitch}
                  disabled={configuring || !connected}
                  sx={{ mt: 2 }}
                >
                  Execute Glitch (g)
                </Button>
              </Box>

            </Paper>
          </>
        )}
      </Container>
    </ThemeProvider>
  );
}

export default App;