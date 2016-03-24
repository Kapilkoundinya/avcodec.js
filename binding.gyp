{
  "targets": [
    {
      "target_name": "decode",
      "type": "executable",
      "sources": ["decode.cc"],
      "defines": ["__STDC_CONSTANT_MACROS"],
      "include_dirs": [
        "/usr/local/include",
      ],
      "libraries": [
        "-lavcodec", "-lavformat", "-L/usr/local/lib"
      ],
      "xcode_settings": {
        "OTHER_CFLAGS": [
	  "-Wno-reserved-user-defined-literal",
	  "-Wno-deprecated-declarations",
        ],
      },
    }
  ]
}