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
      dispatch(block, callback);
      // use block
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
  decoder.on('close', code => callback('done', code));
}

decode('breathing.mp4', () => {});
