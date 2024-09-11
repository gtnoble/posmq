import { EventEmitter } from 'node:events';
import fs from 'node:fs';
import { createRequire } from 'node:module';

const fileConstants = fs.constants;

const MSG_MAX_PATH = "/proc/sys/fs/mqueue/msg_max";
export const MAX_MESSAGE_QUEUE_LENGTH = parseInt(
  fs.readFileSync(MSG_MAX_PATH, {encoding: "utf8"}).trim()
);

const MSGSIZE_MAX_PATH = "/proc/sys/fs/mqueue/msgsize_max";
export const MAX_MESSAGE_SIZE = parseInt(
  fs.readFileSync(MSGSIZE_MAX_PATH, {encoding: "utf8"}).trim()
);

export type MqName = `/${string}`

export interface TimeSpec {
  seconds: number,
  nanoseconds: number
}
export interface LowLevelMq {
  openPosixMq: (
    name: MqName, 
    oflags: number, 
    maxMessages: number, 
    maxMessageSize: number
  ) => number | string,
  closePosixMq: (mqDescriptor: number) => string | null,
  sendPosixMq: (
    mqDescriptor: number, 
    message: Buffer, 
    priority: number, 
    timeout?: TimeSpec
  ) => string | null,
  receivePosixMq: (
    mqDescriptor: number, 
    messageLength: number, 
    timeout?: TimeSpec
  ) => Buffer | string,
  posixMqAttributes: (mqDescriptor: number) => {
    flags: number; 
    maxMessages: number; 
    maxMessageSize: number; 
    currentMessageCount: number
  },
  posixMqNotify: (mqDescriptor: number) => string | null,
  posixMqUnlink: (mqName: string) => null
}

const require = createRequire(import.meta.filename);
const LowLevelPosixMq: LowLevelMq = require(
  `../build/${process.env.NODE_ENV === "dev" ? "Debug" : "Release"}/posixMq.node`
);

export type FopenFlags = "r" | "a" | "r+" | "a+";

function makeTimespec(posixTimeMs: number) {
  const posixTimeSeconds = posixTimeMs / 1000.0;
  return {
    seconds: Math.floor(posixTimeSeconds),
    nanoseconds: Math.round(posixTimeSeconds % 1000 * 1E9)
  };
}

function fileFlagsToOflags(flags: FopenFlags | number): number {
  let oflags: number;
  if (typeof flags === "string") {
    switch (flags) {
      case "r":
        oflags = fileConstants.O_RDONLY;
        break;
      case "a":
        oflags = fileConstants.O_WRONLY | fileConstants.O_CREAT;
        break;
      case "r+":
        oflags = fileConstants.O_RDWR;
        break;
      case "a+":
        oflags = fileConstants.O_RDWR | fileConstants.O_CREAT;
        break;
      default:
        throw new Error(
          `error: "${flags}" is an invalid flags string. See fopen docs for valid strings.`
        );
    }
  }
  else if (typeof flags === "number") {
    oflags = flags;
  }
  else {
    throw new Error(
      "error: flags must be either an 'fopen' flags string or an 'open' oflags number"
    );
  }
  return oflags;
}

function validateMqName(name: MqName) {
  if (name[0] !== "/") {
    throw new Error("error: the first character in a posix message queue name must be '/' (slash).");
  }
  if (name.slice(1).indexOf("/") !== -1) {
    throw new Error("error: '/' can only appear at the beginning of a posix message queue name.");
  }
}

function throwCError(message: string, code: string) {
  throw new Error(`error: ${message}: error code: ${code}`);
}

export class PosixMq extends EventEmitter {
  mqDescriptor: number | null = null;
  readonly flags: number;
  readonly maxMessages: number;
  readonly maxMessageSize: number;

  constructor(
    name: MqName, 
    flags: FopenFlags | number, 
    maxMessages: number, 
    messageSize: number,
    blockingIo?: boolean
  ) {
    super();

    let oflags = fileFlagsToOflags(flags);
    if (! blockingIo) {
      oflags |= fileConstants.O_NONBLOCK;
    }
    validateMqName(name);
    
    if (messageSize <= 0) {
      throw new Error(
        "error: message size must be greater than zero"
      )
    }
    if (maxMessages <= 0) {
      throw new Error(
        "error: maximum messages must be greater than zero"
      )
    }

    if (messageSize > MAX_MESSAGE_SIZE) {
      throw new Error(
        "error: can't create a message queue with a size larger than the system maximum size: " +
        `message size: ${messageSize}: system max size: ${MAX_MESSAGE_SIZE}`
      );
    }
    if (maxMessages > MAX_MESSAGE_QUEUE_LENGTH) {
      throw new Error(
        "error: can't create a message queue with a maximum length greater than the system maximum length: " +
        `message queue length: ${maxMessages}: system max length: ${MAX_MESSAGE_QUEUE_LENGTH}`
      );
    }

    const messageQueueOpenResult = LowLevelPosixMq.openPosixMq(name, oflags, maxMessages, messageSize);
    if (typeof messageQueueOpenResult === 'number') {
      this.mqDescriptor = messageQueueOpenResult;
    }
    else if (typeof messageQueueOpenResult === 'string') {
      const throwOpenError = (message: string) => throwCError(`unable to open message queue: ${message}`, messageQueueOpenResult);
      switch (messageQueueOpenResult) {
        case "EACCES":
          throwOpenError(
            "caller does not have permission to open in the specified mode"
          );
          break;
        case "EEXIST":
          throwOpenError(`a message queue already exists`);
          break;
        case "EMFILE":
          throwOpenError("too many file and message descriptors open for this process");
          break;
        case "ENAMETOOLONG":
          throwOpenError("message queue name is too long");
          break;
        case "ENFILE":
          throwOpenError("too many file and message descriptors open for this system");
          break;
        case "ENOENT":
          throwOpenError("file does not exist");
          break;
        case "ENOMEM":
          throwOpenError("insufficient memory");
          break;
        case "ENOSPC":
          throwOpenError("insufficient space for the creation of a new message queue. probably because queues_max has been reached");
          break;
        default:
          throwOpenError("an unexpected error occured")
      }
    }
    
    this.flags = this.attributes.flags;
    this.maxMessages = this.attributes.maxMessages;
    this.maxMessageSize = this.attributes.maxMessageSize;
    
  }
  
  listen() {
    if (this.mqDescriptor === null) {
      throw new Error("error: can't listen to a closed message queue");
    }
    if (this.blockingIo === true) {
      throw new Error("error: can't listen for messages when IO is blocking");
    }
    process.on('SIGUSR2', () => {
      const messages: Buffer[] = [];
      let nextMessage: Buffer | undefined;
      while ((nextMessage = this.receive()) !== undefined) {
        messages.push(nextMessage);
      }
      this.emit('messages', messages)
    })
  }
  

  
  get attributes() {
    if (this.mqDescriptor === null) {
      throw new Error("error: can't get attributes of a closed posix message queue.");
    }
    
    const messageQueueAttributesResult = LowLevelPosixMq.posixMqAttributes(this.mqDescriptor);
    if (typeof messageQueueAttributesResult === 'string') {
      throw new Error(`error: can't get message queue attributes: error code: ${messageQueueAttributesResult}`);
    }
    else {
      return messageQueueAttributesResult;
    }
  }
  
  get blockingIo(): boolean {
    return ! (this.flags === fileConstants.O_NONBLOCK);
  }
  
  close(): void {
    if (this.mqDescriptor === null) {
      throw new Error("error: can't close an already closed posix message queue.");
    }

    const messageQueueCloseResult = LowLevelPosixMq.closePosixMq(this.mqDescriptor);
    if (typeof messageQueueCloseResult === "string") {
      throw new Error(`error: unable to close message queue: error code: ${messageQueueCloseResult}`)
    }
    else {
      this.mqDescriptor = null;
    }
  }
  
  send(message: Buffer, priority: number = 0, timeout?: number): boolean {
    if (this.mqDescriptor === null) {
      throw new Error("error: can't send to a closed posix message queue.");
    }

    let timespecTimeout
    if (timeout) {
      timespecTimeout = makeTimespec(timeout);
    }
    const messageQueueSendResult = LowLevelPosixMq.sendPosixMq(
      this.mqDescriptor, message, priority, timespecTimeout
    );

    if (typeof messageQueueSendResult === 'string') {
      if (messageQueueSendResult === "EAGAIN" && this.blockingIo) {
        return false;
      }
      if (messageQueueSendResult === "EMSGSIZE") {
        const maxMessageSize = this.maxMessageSize;
        throw new Error(
          "error: can't send a message larger than the max size for this message queue: " +
          `message size: ${message.length}: max size ${maxMessageSize}`);
      }
      throw new Error(`error: unable to send message: error code: ${messageQueueSendResult}`);
    }
    return true;
  }
  
  receive(timeout?: number): Buffer | undefined {
    if (this.mqDescriptor === null) {
      throw new Error("error: can't receive from a closed posix message queue");
    }
    
    let timespecTimeout;
    if (timeout) {
      timespecTimeout = makeTimespec(timeout);
    }
    const messageQueueReceiveResult = LowLevelPosixMq.receivePosixMq(
      this.mqDescriptor, this.maxMessageSize, timespecTimeout
    );
    if (typeof messageQueueReceiveResult === 'string') {
      if (messageQueueReceiveResult === "EAGAIN" && this.blockingIo) {
        return undefined;
      }
      else {
        throw new Error(`error: unable to receive message: error code: ${messageQueueReceiveResult}`);
      }
    }
    else {
      return messageQueueReceiveResult;
    }
  }
  
  static unlink(mqName: string, force?: boolean): void {
    const messageQueueUnlinkResult = LowLevelPosixMq.posixMqUnlink(mqName);
    
    if (typeof messageQueueUnlinkResult === 'string') {
      switch (messageQueueUnlinkResult) {
        case "EACCES":
          throw new Error(`error: not authorized to unlink message queue with name ${mqName}`);
          break;
        case "ENAMETOOLONG":
          throw new Error(`error: message queue name "${mqName}" is too long`);
          break;
        case "ENOENT":
          if (force)
            break;
          throw new Error(`error: there is no message queue with name "${mqName}"`);
          break;
        default:
          throw new Error(
            `error: unable to unlink message queue: error code: ${messageQueueUnlinkResult}`
          )
      }
    }
  }
}

