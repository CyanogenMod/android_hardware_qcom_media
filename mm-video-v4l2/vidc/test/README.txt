=======================================================
msm-vidc-test test program
=======================================================

Description:
This is a test program to verify the vidc V4L2 kernel driver. It executes the
operations and related IOCTL's described in the configuration file.

It requires vidc decoder device at: /dev/video32
It requires vidc encoder device at: /dev/video33
It requires ION driver(/dev/ion)

Parameters:
It requires a configuration file to be provided as a parameter, all others
parameters are optional:
        -c, --config <file>    Configuration file (required)
        -v, --verbose <#>      0 minimal verbosity, 1 to include details, 2 to debug messages
        -n,                    Nominal test (default)
        -r <#times>,           Repeat test #times
        -h, --help             Print this menu

Return:
It will return 0 if all test cases succeed otherwise it
returns a value < 0.

Targets:
        8x74, 8974, 8092


Configurations file parameters and sample files:

Required parameters in configuration file:
input_file     # Valid input file that shall match the codec_type or yuv format
input_height   # Height
input_width    # Width
codec_type     # H.264 | MPEG4 | VP8
output_file    # output file name
device_mode    # DECODE | ENCODE
frame_count    # Minimum number of output frames to be generated.

Optional DECODE parameters:
read_mode      # DEFAULT | FIX | ARBITRARY
               # DEFAULT::
               #  mpeg4: Read in chunks divided by start code 0x000001B6
               #  vp8: Read in chunks base on the ivf header
               #  h.264: Read base on NALu's
               # FIX::
               #  Read a predefined number of bytes from the input file.
               #  Requires "read_bytes" and optionally "fix_buf_size_file".
               #  Intended to be used to test frame assembly feature.
               #  If "fix_buf_size_file" NOT available uses "read_bytes"
               #  values.
               #  If "fix_buf_size_file" available it uses the values provided
               #  by the file until "frame_count" or not enough values to reach
               #  "frame_count", afterwards continue using "read_bytes" value.
               # ARBITRARY::
               #  Read in an arbitrary but reproducible way the input file to
               #  later queue this bytes. It uses internally rand() and
               #  "random_seed" option may be used to set the seed otherwise
               #  seed will be zero.
               #  Intended to be used to test frame assembly feature.

read_bytes     # Integer number used in combination with "read_mode : FIX" to
               # set the number of bytes to be read and later queue by buffer.

fix_buf_size_file # Provides a list with the amount of bytes to be read from
               # the "input_file", each line of the provided file corresponds
               # to the amount of bytes to be queue per buffer.
               # Must be used in combination with "read_mode : FIX".
               # See "Sample fix_buf_size_file" below.

ring_num_headers  # Set the number of header buffers to be use by the ring
               # buffer.

ring_buf_size  # Set the size of the ring buffer, shall be at least the
               # sizeimage value returned by the driver otherwise will be set
               # sizeimage, which is the worst case scenario size (e.g. 1080p)
               # for a single buffer. (e.g. 6291456 in case 1080p is the MAX
               # supported)

random_seed    # Set the seed for the rand() function used internally.

marker_flag    # Set marker flag to be propagated by FW in input buffer ETB
               # to output buffer FBD
               # 1 - V4L2_QCOM_BUF_TS_CONTINUITY
               # 2 - V4L2_QCOM_BUF_TS_ERROR
               # 3 - V4L2_QCOM_BUF_TS_CONTINUITY | V4L2_QCOM_BUF_TS_ERROR

errors_before_stop  # Set the number of continue empty buffer done (EBD)
               # errors without consuming data before taking the decision
               # of closing the session. The errors received are consider
               # as helpers for the decision of doing a codec switch, in
               # this case the application doesn't do a codec switch it
               # just close the session when the number of continue errors
               # are reach.


SEQUENCE commands at configuration file, this commands are run in order from
top to bottom:
OPEN
    # Open device driver.
    # open: "/dev/video32" or "/dev/video33"
    # Options: none.

SUBSCRIBE_EVENT
    # Subscribe to the default events
    # VIDIOC_SUBSCRIBE_EVENT
    # Options: none.

QUERY_CAP
    # Query Capabilities of the driver
    # IOCTL: VIDIOC_QUERYCAP
    # Options: none.

ENUM_FORMATS
    # Enumerate formats
    # IOCTL: ENUM_FORMATS
    # Options: OUTPUT | CAPTURE

SET_FMT
    # Set format
    # IOCTL: VIDIOC_S_FMT
    # Options: OUTPUT | CAPTURE

GET_FMT
    # Get Format
    # IOCTL: VIDIOC_G_FMT
    # Options: OUTPUT | CAPTURE

GET_BUFREQ
    # Request buffer call
    # IOCTL: VIDIOC_REQBUFS
    # Options: OUTPUT | CAPTURE

SET_CTRL
    # Set controls
    # IOCTL: VIDIOC_S_CTRL
    # Options DECODE:
    #   CONTINUE_DATA_TRANSFER
    #       id: V4L2_CID_MPEG_VIDC_VIDEO_CONTINUE_DATA_TRANSFER
    #       val: 0 | 1
    #   ALLOC_INPUT_TYPE
    #       id: V4L2_CID_MPEG_VIDC_VIDEO_ALLOC_MODE_INPUT
    #       val: 0 | 1
    #   FRAME_ASSEMBLY
    #       id: V4L2_CID_MPEG_VIDC_VIDEO_FRAME_ASSEMBLY
    #       val: 0 | 1
    #   VIDEO_EXTRADATA STREAM_USERDATA
    #       id: V4L2_MPEG_VIDC_EXTRADATA_STREAM_USERDATA
    #   SCS_THRESHOLD
    #       id: V4L2_CID_MPEG_VIDC_VIDEO_SCS_THRESHOLD
    #       val: 1 -> INT_MAX

PREPARE_BUFS CAPTURE
    # Prepare buffers for selected port
    # IOCTL: VIDIOC_PREPARE_BUF
    # Options: OUTPUT | CAPTURE

ALLOC_RING_BUF
    # Ring buffer allocation and prepare OUTPUT port ring buffer
    # IOCTL: VIDIOC_PREPARE_BUF
    # Options: none.

STREAM_ON
    # Do stream on selected port.
    # IOCTL: VIDIOC_STREAMON
    # Options: OUTPUT | CAPTURE

QUEUE
    # Queue a buffer in selected port.
    # IOCTL: VIDIOC_QBUF
    # Options: OUTPUT | CAPTURE

RUN
    # The RUN command starts the automatic buffer flow in both ports.
    # It will stop until "frame_count" is reach or end of input file.
    # Also it will exit if an error.
    # The IOCTLs used while in run are:
    # VIDIOC_QBUF, VIDIOC_QBUF, VIDIOC_DQEVENT and poll function from the driver.
    # Options: none.

SLEEP
    # Will pause the execution using the sleep() call.
    # Options: # of seconds to sleep.



Configurations file samples:
Sample 1, Basic video decoder:
input_file         : /mnt/sdcard/test/car.264
input_height       : 144
input_width        : 176
codec_type         : H.264
output_file        : /mnt/sdcard/test/output.yuv
device_mode        : DECODE
frame_count        : 100

SEQUENCE           : OPEN
SEQUENCE           : SUBSCRIBE_EVENT
SEQUENCE           : QUERY_CAP
SEQUENCE           : ENUM_FORMATS CAPTURE
SEQUENCE           : ENUM_FORMATS OUTPUT
SEQUENCE           : SET_FMT OUTPUT
SEQUENCE           : SET_CTRL CONTINUE_DATA_TRANSFER 1
SEQUENCE           : GET_FMT CAPTURE
SEQUENCE           : GET_FMT OUTPUT
SEQUENCE           : SET_FMT OUTPUT
SEQUENCE           : SET_FMT CAPTURE
SEQUENCE           : GET_BUFREQ OUTPUT
SEQUENCE           : GET_BUFREQ CAPTURE
SEQUENCE           : PREPARE_BUFS OUTPUT
SEQUENCE           : PREPARE_BUFS CAPTURE
SEQUENCE           : STREAM_ON CAPTURE
SEQUENCE           : QUEUE OUTPUT
SEQUENCE           : STREAM_ON OUTPUT
SEQUENCE           : RUN


Sample 2, Enable and use ring buffer & frame assembly, set the number of bytes
          to be queue in each buffer base on a file. (see fix_buf_size_file & read_bytes)
          Set number of headers and ring buffer size.
          If the "fix_buf_size_file" is not provided then use "read_bytes" for each buffer.
input_file         : /mnt/sdcard/test/monstersVsAliens_WVGA.264
input_height       : 480
input_width        : 800
codec_type         : H.264
output_file        : /mnt/sdcard/test/ring.yuv
device_mode        : DECODE
read_mode          : FIX
fix_buf_size_file  : /mnt/sdcard/test/monsters_first3_ok.bufsize
read_bytes         : 1500
frame_count        : 200
ring_num_headers   : 32
ring_buf_size      : 6291456

SEQUENCE           : OPEN
SEQUENCE           : SUBSCRIBE_EVENT
SEQUENCE           : QUERY_CAP
SEQUENCE           : ENUM_FORMATS CAPTURE
SEQUENCE           : ENUM_FORMATS OUTPUT
SEQUENCE           : SET_FMT OUTPUT
SEQUENCE           : SET_CTRL CONTINUE_DATA_TRANSFER 1
SEQUENCE           : SET_CTRL ALLOC_INPUT_TYPE 1
SEQUENCE           : SET_CTRL FRAME_ASSEMBLY 1
SEQUENCE           : GET_FMT CAPTURE
SEQUENCE           : GET_FMT OUTPUT
SEQUENCE           : SET_FMT OUTPUT
SEQUENCE           : SET_FMT CAPTURE
SEQUENCE           : GET_BUFREQ OUTPUT
SEQUENCE           : GET_BUFREQ CAPTURE
SEQUENCE           : ALLOC_RING_BUF
SEQUENCE           : PREPARE_BUFS CAPTURE
SEQUENCE           : STREAM_ON CAPTURE
SEQUENCE           : QUEUE OUTPUT
SEQUENCE           : STREAM_ON OUTPUT
SEQUENCE           : RUN


Sample 3, Using ring buffer, queue arbitrary number of bytes in each buffer and set the random seed:
input_file         : /mnt/sdcard/test/monstersVsAliens_WVGA.264
input_height       : 480
input_width        : 800
codec_type         : H.264
output_file        : /mnt/sdcard/test/ring.yuv
device_mode        : DECODE
read_mode          : ARBITRARY
frame_count        : 200
ring_num_headers   : 32
random_seed        : 5

SEQUENCE           : OPEN
SEQUENCE           : SUBSCRIBE_EVENT
SEQUENCE           : QUERY_CAP
SEQUENCE           : ENUM_FORMATS CAPTURE
SEQUENCE           : ENUM_FORMATS OUTPUT
SEQUENCE           : SET_FMT OUTPUT
SEQUENCE           : SET_CTRL CONTINUE_DATA_TRANSFER 1
SEQUENCE           : SET_CTRL ALLOC_INPUT_TYPE 1
SEQUENCE           : SET_CTRL FRAME_ASSEMBLY 1
SEQUENCE           : GET_FMT CAPTURE
SEQUENCE           : GET_FMT OUTPUT
SEQUENCE           : SET_FMT OUTPUT
SEQUENCE           : SET_FMT CAPTURE
SEQUENCE           : GET_BUFREQ OUTPUT
SEQUENCE           : GET_BUFREQ CAPTURE
SEQUENCE           : ALLOC_RING_BUF
SEQUENCE           : PREPARE_BUFS CAPTURE
SEQUENCE           : STREAM_ON CAPTURE
SEQUENCE           : QUEUE OUTPUT
SEQUENCE           : STREAM_ON OUTPUT
SEQUENCE           : RUN

Sample fix_buf_size_file:
17232
23140
7145
5743
8218
11316
