import { range } from "lodash";
import net = require("net")

const PIPE_NAME = "server_pipe"
const PIPE_PATH = "\\\\.\\pipe\\" + PIPE_NAME

const L = console.log

class MessageHeader {
  static MessageIdCounter: number = 0;
  
  constructor(
      public id:number = ++MessageHeader.MessageIdCounter,
      public sourceId:number = 0,
      public clientId:number = 0,
      public size:number = 0) {
    
  }
  
  toDataView(dataView: DataView): DataView {
    dataView.setUint32(0, this.id, true)
    dataView.setUint32(4, this.sourceId, true)
    dataView.setUint32(8, this.clientId, true)
    dataView.setUint32(12, this.size, true)
    return dataView
  }
}

const MessageHeaderLength = 4 + 4 + 4 + 4;

class NamedPipeClient {
  static ClientIdCounter = 0
  
  client: NodeJS.Socket
  
  constructor(
      public clientId: number = ++NamedPipeClient.ClientIdCounter
  ) {
    this.client = net.connect(PIPE_PATH, () => {
      L("Client: on connection, starting to send messages")
      this.sendMessages()
          .catch(err => {
            console.error("Failed to send messages", err)
          })
    }).on("data", function(data) {
      L("Client: on data:", data.toString())
    }).on("end", function() {
      L("Client: on end")
    })
    
    
  }
  
  async sendMessages() {
    await new Promise(resolve => setTimeout(resolve, 100))
    
    for (let i = 1; i < 3;i++) {
      
      const data = `Client(${this.clientId}) Message #${i}`,
          msgHeader = new MessageHeader(i,0,this.clientId,data.length),
          msgLen = MessageHeaderLength + data.length,
          msgBuf = Buffer.alloc(msgLen),
          msgView = new DataView(msgBuf.buffer)
      
      msgHeader.toDataView(msgView)
      msgBuf.write(data, MessageHeaderLength)
      
      this.client.write(msgBuf)
      await new Promise(resolve => setTimeout(resolve, 100))
    }
    
    // client.end()
  }
}

const clients = range(4).map(() => new NamedPipeClient())