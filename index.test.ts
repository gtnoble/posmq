import tap from 'tap';

import * as Pmq from './index.js';

function makeBiggestMq(name: Pmq.MqName, flags: Pmq.FopenFlags = "a+") {
  return new Pmq.PosixMq(name, flags, Pmq.MAX_MESSAGE_QUEUE_LENGTH, Pmq.MAX_MESSAGE_SIZE);
}

tap.test(
  "Test Posix Mq",
  async (tt) => {
      const testMessage = "caulkChisChris";
      tt.ok(Pmq.MAX_MESSAGE_QUEUE_LENGTH > 0, "Must be able to store at least one message");
      tt.ok(Pmq.MAX_MESSAGE_SIZE > testMessage.length, "Message size must be at least one byte");
      
      tt.throws(
        () => makeBiggestMq("//tooManySlashes"), 
        "Must throw when slashes anywhere but at the beginning"
      );
      
      
      tt.test("Test Closing MQ", 
        async (tt) => {
          const mq = makeBiggestMq("/testMq");
          tt.ok(mq.mqDescriptor, "Open Mq should not be closed");
          mq.close();
          tt.notOk(mq.mqDescriptor, "Closed Mq should be closed");
          tt.throws(() => mq.close(), "Should not be able to close a closed Mq");
          tt.throws(() => mq.send(Buffer.from(testMessage, "utf8")), "Should not be able to send to a closed Mq");
          tt.throws(() => mq.receive(), "Should not be able to receive from a closed Mq");
        }
      )
      
      const sentMessage = Buffer.from(testMessage, "utf-8");
      
      tt.test("Test Message Transmission", 
      async (tt) => {
        const mq = new Pmq.PosixMq(
          "/testMq2", 
          "a+", 
          Pmq.MAX_MESSAGE_QUEUE_LENGTH, 
          sentMessage.length
        );
        //const mq = makeBiggestMq("/testMq");
        mq.send(sentMessage);
        const receivedMessage = mq.receive();
        tt.type(receivedMessage, Buffer);
        tt.equal(
          (<Buffer> receivedMessage).toString("utf-8"), 
          sentMessage.toString("utf8"), 
          "Sent and received messages should be the same"
        );
      })
  }
)
