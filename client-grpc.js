// server.js
const grpc = require("@grpc/grpc-js");
const protoLoader = require("@grpc/proto-loader");
const path = require("path");
const net = require("net");

// Load proto file
const PROTO_PATH = path.resolve(__dirname, "src", "grpc", "proto", "service.proto");
const packageDefinition = protoLoader.loadSync(PROTO_PATH);
const pkgDef = grpc.loadPackageDefinition(packageDefinition)
const pipeService = pkgDef.namedpipe.NamedPipeService;

// Windows named pipe path
// const pipePath = "unix:\\\\.\\pipe\\grpc_custom_pipe";
const pipePath = "unix:////./pipe/grpc_custom_pipe";
// Create custom client credentials
class NamedPipeClient {
  constructor(pipePath) {
    this.pipePath = pipePath;
    this.counter = 0;
    this.running = true;
    
    // Create a custom client based on the named pipe transport
    this.client = new pipeService(
      pipePath,
      grpc.credentials.createInsecure(),
      {
        'grpc.default_authority': 'localhost',
        // 'grpc.use_local_subchannel_pool': 1,
        'grpc.channelOverride': {
          'grpc.testing.fixed_address_list': [{ 'address': pipePath }],
        },
      }
    );
  }

  startStreaming() {
    console.log(`Starting bidirectional stream to ${this.pipePath}`);
    
    // Create bidirectional stream
    this.stream = this.client.StreamData();
    
    // Set up event handlers
    this.stream.on('data', (response) => {
      console.log(`Received: ${response.message} (counter: ${response.counter})`);
    });
    
    this.stream.on('end', () => {
      console.log('Server ended stream');
      this.running = false;
    });
    
    this.stream.on('error', (error) => {
      console.error('Stream error:', error);
      this.running = false;
    });
    
    // Start sending messages
    this.sendMessages();
  }
  
  sendMessages() {
    const sendNextMessage = () => {
      if (!this.running) return;
      
      this.counter++;
      const message = { message: `Message ${this.counter}` };
      
      try {
        this.stream.write(message);
        
        if (this.counter % 100 === 0) {
          console.log(`Sent ${this.counter} messages`);
        }
        
        // Schedule next message
        setTimeout(sendNextMessage, 100);
      } catch (err) {
        console.error('Error sending message:', err);
        this.running = false;
      }
    };
    
    // Start sending messages
    sendNextMessage();
  }
  
  stop() {
    console.log('Stopping client...');
    this.running = false;
    
    if (this.stream) {
      try {
        this.stream.end();
      } catch (err) {
        console.error('Error closing stream:', err);
      }
    }
  }
}

// Create and start client
const client = new NamedPipeClient(pipePath);
client.startStreaming();

// Handle graceful shutdown
process.on('SIGINT', () => {
  console.log('Received SIGINT. Shutting down...');
  client.stop();
  setTimeout(() => {
    process.exit(0);
  }, 1000);
});

// Display connection instructions
console.log(`Client connecting to named pipe: ${pipePath}`);
console.log('Press Ctrl+C to exit');
