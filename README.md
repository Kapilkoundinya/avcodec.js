# avcodec

Decode a video into image and audio frames. In the future we will likely
also support encoding.

```javascript
var decode = require('avcodec').decode;

decode('video.mp4', data => console.log(data));
```

> { type: 'video',
>   format: 0,
>   width: 1280,
>   height: 720,
>   buffer: ... }

> { type: 'audio',
>   format: 8,
>   channels: 1,
>   nb_samples: 1024,
>   buffer: ... }

> { type: 'done' }

> { type: 'error',
    message: "Why this failed." }
