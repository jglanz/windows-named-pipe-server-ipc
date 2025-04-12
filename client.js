// const ipc = require('node-ipc').default
//
// ipc.config.id = 'mypipe';
// ipc.config.retry = 1500;
// ipc.config.silent = true;
//
// ipc.serve(
//   function(){
//     ipc.server.on(
//       'message',
//       function(data, socket){
//         console.log('Received message:', data);
//       }
//     );
//   }
// );
//
// ipc.server.start();

const net = require("net")

const PIPE_NAME = "server_pipe"
const PIPE_PATH = "\\\\.\\pipe\\" + PIPE_NAME

const L = console.log

class MessageHeader {
  static MessageIdCounter = 0;
  id = 0
  sourceId = 0
  size = 0
  
  constructor(id = ++MessageHeader.MessageIdCounter, sourceId = 0, size = 0) {
    Object.assign(this, {
      id, sourceId, size
    })
  }
}

const MessageHeaderLength = 4 + 4 + 4;

const client = net.connect(PIPE_PATH, function() {
  L("Client: on connection, starting to send messages")
  sendMessages()
    .catch(err => {
      console.error("Failed to send messages", err)
    })
})

client.on("data", function(data) {
  L("Client: on data:", data.toString())
  // client.end('Thanks!');
})

client.on("end", function() {
  L("Client: on end")
})

async function sendMessages() {
  await new Promise(resolve => setTimeout(resolve, 100))
  
  for (let i = 0; i < 2;i++) {
    
    const data = `Message #${i}`,
      msgLen = MessageHeaderLength + data.length,
      msgBuf = Buffer.alloc(msgLen),
      msgView = new DataView(msgBuf.buffer)
    
    msgView.setUint32(0, data.length, true)
    msgBuf.write(data, 4)
    
    L(`Sending ${i}`)
    client.write(msgBuf)
    await new Promise(resolve => setTimeout(resolve, 100))
  }
  
  client.end()
}