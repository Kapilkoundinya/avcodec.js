var decode = require('../index.js').decode;

exports.testDecode = test => {
  test.expect(146);
  var done = false;
  decode('test/test.mp4', 'rgb', data => {
    test.ok(!done);
    if (data.type == 'video')
      test.ok(data.width == 320 && data.height == 180 && data.format == 0);
    if (data.type == 'audio')
      test.ok(data.channels == 2 && data.nb_samples == 1024 && data.format == 8);
    if (data.type == 'done') {
      test.ok(true);
      test.done();
      done = true;
    }
   });
};
