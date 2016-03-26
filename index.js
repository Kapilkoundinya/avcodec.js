/* -*- Mode: Java; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set shiftwidth=2 tabstop=2 autoindent cindent expandtab: */

var spawn = require('child_process').spawn;
var stream = require('stream');

var video_tag = 0x22057601;
var audio_tag = 0x22057602;

function dispatch(block, callback) {
  console.log(block.length);
}

function decode(videoName, callback) {
  var q = [];
  var q_len = 0;

  var decoder = spawn(__dirname + '/build/Release/decode', [videoName]);

  decoder.stdout.on('data', data => {
    q.push(data);
    q_len += data.length;
    while (true) {
      if (q_len < 4)
        return;
      if (q[0].length >= 4) {
        // Take shortcut when we are waiting for a packet and the first packet
        // is long enough for the entire size field (4 bytes).
        var bytes = q[0].readInt32LE(0);
        if (q_len < bytes)
          return;
      }
      var buffer = Buffer.concat(q);
      var bytes = buffer.readInt32LE(0);
      // We have to check again here in case the first packet is too short
      // and we couldn't read the length field until we concatenated the
      // packets. This is very rare.
      if (buffer.length < bytes)
        return;
      var block = buffer.slice(0, bytes);
      switch(block.readInt32LE(4)) {
      case video_tag:
        callback({
          type: 'video',
          format: block.readInt32LE(8),
          width: block.readInt32LE(12),
          height: block.readInt32LE(16),
          buffer: block.slice(20),
        });
        break;
      case audio_tag:
        callback({
          type: 'audio',
          format: block.readInt32LE(8),
          channels: block.readInt32LE(12),
          nb_samples: block.readInt32LE(16),
          buffer: block.slice(20),
        });
        break;
      default:
        // Something went wrong. Kill the decoder which will report an
        // error to the callback.
        decoder.kill('SIGKILL');
        break;
      }
      // Process the rest of the buffer in the next iteration.
      var buffer = buffer.slice(bytes);
      if (buffer.length) {
        q[0] = buffer;
        q.length = 1;
      } else {
        q.length = 0;
      }
      q_len = buffer.length;
    }
  });
  decoder.stderr.on('data', data => callback({ type: 'error', message: data.toString('utf8') }));
  decoder.on('close', code => callback(code
                                       ? { type: 'error', message: 'decoder error code ' + code }
                                       : { type: 'done' }));
}

module.exports = {
  decode: decode,
};
