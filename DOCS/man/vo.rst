VIDEO OUTPUT DRIVERS
====================

Video output drivers are interfaces to different video output facilities. The
syntax is:

``--vo=<driver1[:suboption1[=value]:...],driver2,...[,]>``
    Specify a priority list of video output drivers to be used.

If the list has a trailing ',', mpv will fall back on drivers not contained
in the list. Suboptions are optional and can mostly be omitted.

You can also set defaults for each driver. The defaults are applied before the
normal driver parameters.

``--vo-defaults=<driver1[:parameter1:parameter2:...],driver2,...>``
    Set defaults for each driver.

    Deprecated. No replacement.

.. note::

    See ``--vo=help`` for a list of compiled-in video output drivers.

    The recommended output driver is ``--vo=opengl``. All other drivers are
    for compatibility or special purposes. By default, ``--vo=opengl`` is used,
    but if that appears not to work, it fallback to other drivers (in the same
    order as listed by ``--vo=help``).

Available video output drivers are:

``xv`` (X11 only)
    Uses the XVideo extension to enable hardware-accelerated display. This is
    the most compatible VO on X, but may be low-quality, and has issues with
    OSD and subtitle display.

    .. note:: This driver is for compatibility with old systems.

    ``adaptor=<number>``
        Select a specific XVideo adapter (check xvinfo results).
    ``port=<number>``
        Select a specific XVideo port.
    ``ck=<cur|use|set>``
        Select the source from which the color key is taken (default: cur).

        cur
          The default takes the color key currently set in Xv.
        use
          Use but do not set the color key from mpv (use the ``--colorkey``
          option to change it).
        set
          Same as use but also sets the supplied color key.

    ``ck-method=<none|man|bg|auto>``
        Sets the color key drawing method (default: man).

        none
          Disables color-keying.
        man
          Draw the color key manually (reduces flicker in some cases).
        bg
          Set the color key as window background.
        auto
          Let Xv draw the color key.

    ``colorkey=<number>``
        Changes the color key to an RGB value of your choice. ``0x000000`` is
        black and ``0xffffff`` is white.

    ``buffers=<number>``
        Number of image buffers to use for the internal ringbuffer (default: 2).
        Increasing this will use more memory, but might help with the X server
        not responding quickly enough if video FPS is close to or higher than
        the display refresh rate.

``x11`` (X11 only)
    Shared memory video output driver without hardware acceleration that works
    whenever X11 is present.

    .. note:: This is a fallback only, and should not be normally used.

``vdpau`` (X11 only)
    Uses the VDPAU interface to display and optionally also decode video.
    Hardware decoding is used with ``--hwdec=vdpau``.

    .. note::

        Earlier versions of mpv (and MPlayer, mplayer2) provided sub-options
        to tune vdpau post-processing, like ``deint``, ``sharpen``, ``denoise``,
        ``chroma-deint``, ``pullup``, ``hqscaling``. These sub-options are
        deprecated, and you should use the ``vdpaupp`` video filter instead.

    ``sharpen=<-1-1>``
        (Deprecated. See note about ``vdpaupp``.)

        For positive values, apply a sharpening algorithm to the video, for
        negative values a blurring algorithm (default: 0).
    ``denoise=<0-1>``
        (Deprecated. See note about ``vdpaupp``.)

        Apply a noise reduction algorithm to the video (default: 0; no noise
        reduction).
    ``deint=<-4-4>``
        (Deprecated. See note about ``vdpaupp``.)

        Select deinterlacing mode (default: 0). In older versions (as well as
        MPlayer/mplayer2) you could use this option to enable deinterlacing.
        This doesn't work anymore, and deinterlacing is enabled with either
        the ``d`` key (by default mapped to the command ``cycle deinterlace``),
        or the ``--deinterlace`` option. Also, to select the default deint mode,
        you should use something like ``--vf-defaults=vdpaupp:deint-mode=temporal``
        instead of this sub-option.

        0
            Pick the ``vdpaupp`` video filter default, which corresponds to 3.
        1
            Show only first field.
        2
            Bob deinterlacing.
        3
            Motion-adaptive temporal deinterlacing. May lead to A/V desync
            with slow video hardware and/or high resolution.
        4
            Motion-adaptive temporal deinterlacing with edge-guided spatial
            interpolation. Needs fast video hardware.
    ``chroma-deint``
        (Deprecated. See note about ``vdpaupp``.)

        Makes temporal deinterlacers operate both on luma and chroma (default).
        Use no-chroma-deint to solely use luma and speed up advanced
        deinterlacing. Useful with slow video memory.
    ``pullup``
        (Deprecated. See note about ``vdpaupp``.)

        Try to apply inverse telecine, needs motion adaptive temporal
        deinterlacing.
    ``hqscaling=<0-9>``
        (Deprecated. See note about ``vdpaupp``.)

        0
            Use default VDPAU scaling (default).
        1-9
            Apply high quality VDPAU scaling (needs capable hardware).
    ``fps=<number>``
        Override autodetected display refresh rate value (the value is needed
        for framedrop to allow video playback rates higher than display
        refresh rate, and for vsync-aware frame timing adjustments). Default 0
        means use autodetected value. A positive value is interpreted as a
        refresh rate in Hz and overrides the autodetected value. A negative
        value disables all timing adjustment and framedrop logic.
    ``composite-detect``
        NVIDIA's current VDPAU implementation behaves somewhat differently
        under a compositing window manager and does not give accurate frame
        timing information. With this option enabled, the player tries to
        detect whether a compositing window manager is active. If one is
        detected, the player disables timing adjustments as if the user had
        specified ``fps=-1`` (as they would be based on incorrect input). This
        means timing is somewhat less accurate than without compositing, but
        with the composited mode behavior of the NVIDIA driver, there is no
        hard playback speed limit even without the disabled logic. Enabled by
        default, use ``no-composite-detect`` to disable.
    ``queuetime_windowed=<number>`` and ``queuetime_fs=<number>``
        Use VDPAU's presentation queue functionality to queue future video
        frame changes at most this many milliseconds in advance (default: 50).
        See below for additional information.
    ``output_surfaces=<2-15>``
        Allocate this many output surfaces to display video frames (default:
        3). See below for additional information.
    ``colorkey=<#RRGGBB|#AARRGGBB>``
        Set the VDPAU presentation queue background color, which in practice
        is the colorkey used if VDPAU operates in overlay mode (default:
        ``#020507``, some shade of black). If the alpha component of this value
        is 0, the default VDPAU colorkey will be used instead (which is usually
        green).
    ``force-yuv``
        Never accept RGBA input. This means mpv will insert a filter to convert
        to a YUV format before the VO. Sometimes useful to force availability
        of certain YUV-only features, like video equalizer or deinterlacing.

    Using the VDPAU frame queuing functionality controlled by the queuetime
    options makes mpv's frame flip timing less sensitive to system CPU load and
    allows mpv to start decoding the next frame(s) slightly earlier, which can
    reduce jitter caused by individual slow-to-decode frames. However, the
    NVIDIA graphics drivers can make other window behavior such as window moves
    choppy if VDPAU is using the blit queue (mainly happens if you have the
    composite extension enabled) and this feature is active. If this happens on
    your system and it bothers you then you can set the queuetime value to 0 to
    disable this feature. The settings to use in windowed and fullscreen mode
    are separate because there should be no reason to disable this for
    fullscreen mode (as the driver issue should not affect the video itself).

    You can queue more frames ahead by increasing the queuetime values and the
    ``output_surfaces`` count (to ensure enough surfaces to buffer video for a
    certain time ahead you need at least as many surfaces as the video has
    frames during that time, plus two). This could help make video smoother in
    some cases. The main downsides are increased video RAM requirements for
    the surfaces and laggier display response to user commands (display
    changes only become visible some time after they're queued). The graphics
    driver implementation may also have limits on the length of maximum
    queuing time or number of queued surfaces that work well or at all.

``direct3d_shaders`` (Windows only)
    Video output driver that uses the Direct3D interface.

    .. note:: This driver is for compatibility with systems that don't provide
              proper OpenGL drivers.

    ``prefer-stretchrect``
        Use ``IDirect3DDevice9::StretchRect`` over other methods if possible.

    ``disable-stretchrect``
        Never render the video using ``IDirect3DDevice9::StretchRect``.

    ``disable-textures``
        Never render the video using D3D texture rendering. Rendering with
        textures + shader will still be allowed. Add ``disable-shaders`` to
        completely disable video rendering with textures.

    ``disable-shaders``
        Never use shaders when rendering video.

    ``only-8bit``
        Never render YUV video with more than 8 bits per component.
        Using this flag will force software conversion to 8-bit.

    ``disable-texture-align``
        Normally texture sizes are always aligned to 16. With this option
        enabled, the video texture will always have exactly the same size as
        the video itself.


    Debug options. These might be incorrect, might be removed in the future,
    might crash, might cause slow downs, etc. Contact the developers if you
    actually need any of these for performance or proper operation.

    ``force-power-of-2``
        Always force textures to power of 2, even if the device reports
        non-power-of-2 texture sizes as supported.

    ``texture-memory=<mode>``
        Only affects operation with shaders/texturing enabled, and (E)OSD.
        Possible values:

        ``default`` (default)
            Use ``D3DPOOL_DEFAULT``, with a ``D3DPOOL_SYSTEMMEM`` texture for
            locking. If the driver supports ``D3DDEVCAPS_TEXTURESYSTEMMEMORY``,
            ``D3DPOOL_SYSTEMMEM`` is used directly.

        ``default-pool``
            Use ``D3DPOOL_DEFAULT``. (Like ``default``, but never use a
            shadow-texture.)

        ``default-pool-shadow``
            Use ``D3DPOOL_DEFAULT``, with a ``D3DPOOL_SYSTEMMEM`` texture for
            locking. (Like ``default``, but always force the shadow-texture.)

        ``managed``
            Use ``D3DPOOL_MANAGED``.

        ``scratch``
            Use ``D3DPOOL_SCRATCH``, with a ``D3DPOOL_SYSTEMMEM`` texture for
            locking.

    ``swap-discard``
        Use ``D3DSWAPEFFECT_DISCARD``, which might be faster.
        Might be slower too, as it must(?) clear every frame.

    ``exact-backbuffer``
        Always resize the backbuffer to window size.

``direct3d`` (Windows only)
    Same as ``direct3d_shaders``, but with the options ``disable-textures``
    and ``disable-shaders`` forced.

    .. note:: This driver is for compatibility with old systems.

``opengl``
    OpenGL video output driver. It supports extended scaling methods, dithering
    and color management.

    See `OpenGL renderer options`_ for options specific to this VO.

    By default, it tries to use fast and fail-safe settings. Use the
    ``opengl-hq`` profile to use this driver with defaults set to high
    quality rendering. (This profile is also the replacement for
    ``--vo=opengl-hq``.) The profile can be applied with ``--profile=opengl-hq``
    and its contents can be viewed with ``--show-profile=opengl-hq``.

    Requires at least OpenGL 2.1.

    Some features are available with OpenGL 3 capable graphics drivers only
    (or if the necessary extensions are available).

    OpenGL ES 2.0 and 3.0 are supported as well.

    Hardware decoding over OpenGL-interop is supported to some degree. Note
    that in this mode, some corner case might not be gracefully handled, and
    color space conversion and chroma upsampling is generally in the hand of
    the hardware decoder APIs.

    ``opengl`` makes use of FBOs by default. Sometimes you can achieve better
    quality or performance by changing the ``--opengl-fbo-format`` option to
    ``rgb16f``, ``rgb32f`` or ``rgb``. Known problems include Mesa/Intel not
    accepting ``rgb16``, Mesa sometimes not being compiled with float texture
    support, and some OS X setups being very slow with ``rgb16`` but fast
    with ``rgb32f``. If you have problems, you can also try enabling the
    ``--opengl-dumb-mode=yes`` option.

``sdl``
    SDL 2.0+ Render video output driver, depending on system with or without
    hardware acceleration. Should work on all platforms supported by SDL 2.0.
    For tuning, refer to your copy of the file ``SDL_hints.h``.

    .. note:: This driver is for compatibility with systems that don't provide
              proper graphics drivers, or which support GLES only.

    ``sw``
        Continue even if a software renderer is detected.

    ``switch-mode``
        Instruct SDL to switch the monitor video mode when going fullscreen.

``vaapi``
    Intel VA API video output driver with support for hardware decoding. Note
    that there is absolutely no reason to use this, other than wanting to use
    hardware decoding to save power on laptops, or possibly preventing video
    tearing with some setups.

    .. note:: This driver is for compatibility with crappy systems. You can
              use vaapi hardware decoding with ``--vo=opengl`` too.

    ``scaling=<algorithm>``
        default
            Driver default (mpv default as well).
        fast
            Fast, but low quality.
        hq
            Unspecified driver dependent high-quality scaling, slow.
        nla
            ``non-linear anamorphic scaling``

    ``deint-mode=<mode>``
        Select deinterlacing algorithm. Note that by default deinterlacing is
        initially always off, and needs to be enabled with the ``d`` key
        (default key binding for ``cycle deinterlace``).

        This option doesn't apply if libva supports video post processing (vpp).
        In this case, the default for ``deint-mode`` is ``no``, and enabling
        deinterlacing via user interaction using the methods mentioned above
        actually inserts the ``vavpp`` video filter. If vpp is not actually
        supported with the libva backend in use, you can use this option to
        forcibly enable VO based deinterlacing.

        no
            Don't allow deinterlacing (default for newer libva).
        first-field
            Show only first field (going by ``--field-dominance``).
        bob
            bob deinterlacing (default for older libva).

    ``scaled-osd=<yes|no>``
        If enabled, then the OSD is rendered at video resolution and scaled to
        display resolution. By default, this is disabled, and the OSD is
        rendered at display resolution if the driver supports it.

``null``
    Produces no video output. Useful for benchmarking.

    Usually, it's better to disable video with ``--no-video`` instead.

    ``fps=<value>``
        Simulate display FPS. This artificially limits how many frames the
        VO accepts per second.

``caca``
    Color ASCII art video output driver that works on a text console.

    .. note:: This driver is a joke.

``image``
    Output each frame into an image file in the current directory. Each file
    takes the frame number padded with leading zeros as name.

    ``format=<format>``
        Select the image file format.

        jpg
            JPEG files, extension .jpg. (Default.)
        jpeg
            JPEG files, extension .jpeg.
        png
            PNG files.
        ppm
            Portable bitmap format.
        pgm
            Portable graymap format.
        pgmyuv
            Portable graymap format, using the YV12 pixel format.
        tga
            Truevision TGA.

    ``png-compression=<0-9>``
        PNG compression factor (speed vs. file size tradeoff) (default: 7)
    ``png-filter=<0-5>``
        Filter applied prior to PNG compression (0 = none; 1 = sub; 2 = up;
        3 = average; 4 = Paeth; 5 = mixed) (default: 5)
    ``jpeg-quality=<0-100>``
        JPEG quality factor (default: 90)
    ``(no-)jpeg-progressive``
        Specify standard or progressive JPEG (default: no).
    ``(no-)jpeg-baseline``
        Specify use of JPEG baseline or not (default: yes).
    ``jpeg-optimize=<0-100>``
        JPEG optimization factor (default: 100)
    ``jpeg-smooth=<0-100>``
        smooth factor (default: 0)
    ``jpeg-dpi=<1->``
        JPEG DPI (default: 72)
    ``outdir=<dirname>``
        Specify the directory to save the image files to (default: ``./``).

``wayland`` (Wayland only)
    Wayland shared memory video output as fallback for ``opengl``.

    .. note:: This driver is for compatibility with systems that don't provide
              working OpenGL drivers.

    ``alpha``
        Use a buffer format that supports videos and images with alpha
        information
    ``rgb565``
        Use RGB565 as buffer format. This format is implemented on most
        platforms, especially on embedded where it is far more efficient then
        RGB8888.
    ``triple-buffering``
        Use 3 buffers instead of 2. This can lead to more fluid playback, but
        uses more memory.

``opengl-cb``
    For use with libmpv direct OpenGL embedding; useless in any other contexts.
    (See ``<mpv/opengl_cb.h>``.)

    This also supports many of the options the ``opengl`` VO has.

``rpi`` (Raspberry Pi)
    Native video output on the Raspberry Pi using the MMAL API.

    ``display=<number>``
        Select the display number on which the video overlay should be shown
        (default: 0).

    ``layer=<number>``
        Select the dispmanx layer on which the video overlay should be shown
        (default: -10). Note that mpv will also use the 2 layers above the
        selected layer, to handle the window background and OSD. Actual video
        rendering will happen on the layer above the selected layer.

    ``background=<yes|no>``
        Whether to render a black background behind the video (default: no).
        Normally it's better to kill the console framebuffer instead, which
        gives better performance.

    ``osd=<yes|no>``
        Enabled by default. If disabled with ``no``, no OSD layer is created.
        This also means there will be no subtitles rendered.

``aml`` (Amlogic)
    Native video output on Amlogic chips using amcodec API.

``drm`` (Direct Rendering Manager)
    Video output driver using Kernel Mode Setting / Direct Rendering Manager.
    Should be used when one doesn't want to install full-blown graphical
    environment (e.g. no X). Does not support hardware acceleration (if you
    need this, check the ``drm-egl`` backend for ``opengl`` VO).

    ``connector=<number>``
        Select the connector to use (usually this is a monitor.) If set to -1,
        mpv renders the output on the first available connector. (default: -1)

    ``devpath=<filename>``
        Path to graphic card device.
        (default: /dev/dri/card0)

    ``mode=<number>``
        Mode ID to use (resolution, bit depth and frame rate).
        (default: 0)

