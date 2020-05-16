#include "opengl/opengl.hpp"

struct VideoEGL : VideoDriver, OpenGL {
    VideoEGL& self = *this;
    VideoEGL(Video& super) : VideoDriver(super) { construct(); }
    ~VideoEGL() { destruct(); }

    auto create() -> bool override {
      VideoDriver::exclusive = true;
      VideoDriver::format = "ARGB24";
      return initialize();
    }

    auto driver() -> string override { return "OpenGL 3.2"; }
    auto ready() -> bool override { return _ready; }

    auto hasFullScreen() -> bool override { return true; }
    auto hasMonitor() -> bool override { return true; }
    auto hasContext() -> bool override { return true; }
    auto hasBlocking() -> bool override { return true; }
    auto hasFlush() -> bool override { return true; }
    auto hasShader() -> bool override { return true; }

    auto hasFormats() -> vector<string> override {
      if(_depth == 30) return {"ARGB30", "ARGB24"};
      if(_depth == 24) return {"ARGB24"};
      return {"ARGB24"};  //fallback
    }

    auto setFullScreen(bool fullScreen) -> bool override {
      return initialize();
    }

    auto setMonitor(string monitor) -> bool override {
      return initialize();
    }

    auto setContext(uintptr context) -> bool override {
      return initialize();
    }

    auto setBlocking(bool blocking) -> bool override {
      eglSwapInterval(_eglDisplay, blocking);
      return true;
    }

    auto setFlush(bool flush) -> bool override {
      return true;
    }

    auto setFormat(string format) -> bool override {
      if(format == "ARGB24") {
        OpenGL::inputFormat = GL_RGBA8;
        return initialize();
      }

      if(format == "ARGB30") {
        OpenGL::inputFormat = GL_RGB10_A2;
        return initialize();
      }

      return false;
    }

    auto setShader(string shader) -> bool override {
      OpenGL::setShader(shader);
      return true;
    }

    auto focused() -> bool override {
      return true;
    }

    auto clear() -> void override {
      OpenGL::clear();
      if(_doubleBuffer) eglSwapBuffers(_eglDisplay, _eglSurface);
    }

    auto size(uint& width, uint& height) -> void override {
      if(self.fullScreen) {
        width = _monitorWidth;
        height = _monitorHeight;
      } else {
        XWindowAttributes parent;
        XGetWindowAttributes(_xDisplay, _xParent, &parent);
        width = parent.width;
        height = parent.height;
      }
    }

    auto acquire(uint32_t*& data, uint& pitch, uint width, uint height) -> bool override {
      OpenGL::size(width, height);
      return OpenGL::lock(data, pitch);
    }

    auto release() -> void override {
    }

    auto output(uint width, uint height) -> void override {
      XWindowAttributes window;
      XGetWindowAttributes(_xDisplay, _xWindow, &window);

      XWindowAttributes parent;
      XGetWindowAttributes(_xDisplay, _xParent, &parent);

      if(window.width != parent.width || window.height != parent.height) {
        XResizeWindow(_xDisplay, _xWindow, parent.width, parent.height);
      }

      //convert (0,0) from top-left to bottom-left coordinates
      auto _height = height ? height : _monitorHeight;
      auto _monitorY = parent.height - (this->_monitorY + _height) - (_monitorHeight - _height);

      OpenGL::absoluteWidth = width;
      OpenGL::absoluteHeight = height;
      OpenGL::outputX = self.fullScreen ? _monitorX : 0;
      OpenGL::outputY = self.fullScreen ? _monitorY : 0;
      OpenGL::outputWidth = self.fullScreen ? _monitorWidth : parent.width;
      OpenGL::outputHeight = self.fullScreen ? _monitorHeight : parent.height;
      OpenGL::output();

      if(_doubleBuffer) eglSwapBuffers(_eglDisplay, _eglSurface);
      if(self.flush) glFinish();
    }

    auto poll() -> void override {
      while(XPending(_xDisplay)) {
        XEvent event;
        XNextEvent(_xDisplay, &event);
        if(event.type == Expose) {
          XWindowAttributes attributes;
          XGetWindowAttributes(_xDisplay, _xWindow, &attributes);
          super.doUpdate(attributes.width, attributes.height);
        }
      }
    }

  private:
    auto construct() -> void {
      auto getEglDisplay = [](void* nativeDisplay, const EGLAttrib* attribs) -> EGLDisplay {
        static EGLDisplay(EGLAPIENTRYP getPlatformDisplay)(EGLenum, void*, const EGLAttrib*) = NULL;

        if(!getPlatformDisplay) {
          getPlatformDisplay = decltype(getPlatformDisplay)(eglGetProcAddress("eglGetPlatformDisplayEXT"));
        }

        if(getPlatformDisplay) {
          return getPlatformDisplay(EGL_PLATFORM_X11_EXT, nativeDisplay, attribs);
        }

        return eglGetDisplay((EGLNativeDisplayType) nativeDisplay);
      };

      _xDisplay = XOpenDisplay(nullptr);

      XWindowAttributes attribs{};
      XGetWindowAttributes(_xDisplay, RootWindow(_xDisplay, DefaultScreen(_xDisplay)), &attribs);
      _depth = attribs.depth;

      _eglDisplay = getEglDisplay(_xDisplay, nullptr);
      if(!eglInitialize(_eglDisplay, &_eglVersionMajor, &_eglVersionMinor))
        destruct();
    }

    auto destruct() -> void {
      terminate();
      XCloseDisplay(_xDisplay);
    }

    auto initialize() -> bool {
      terminate();
      if(!self.fullScreen && !self.context) return false;

      //require EGL 1.5+ API
      if(_eglVersionMajor < 1 || (_eglVersionMajor == 1 && _eglVersionMinor < 5)) return false;

      EGLint configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_RED_SIZE, VideoDriver::format == "ARGB30" ? 10 : 8,
        EGL_GREEN_SIZE, VideoDriver::format == "ARGB30" ? 10 : 8,
        EGL_BLUE_SIZE, VideoDriver::format == "ARGB30" ? 10 : 8,
        EGL_ALPHA_SIZE, VideoDriver::format == "ARGB30" ? 2 : 8,
        EGL_NONE
      };

      EGLint configSize = 0;
      if(!eglChooseConfig(_eglDisplay, configAttribs, nullptr, 0, &configSize))
        return false;
      vector<EGLConfig> configs = {};
      configs.resize(configSize);
      if(!eglChooseConfig(_eglDisplay, configAttribs, configs.data(), configs.size(), &configSize))
        return false;

      EGLConfig config = nullptr;
      { // based on egl code from SDL2
        int32_t chosenBitdiff = -1;
        for(auto c : configs) {
          int32_t bitdiff = 0;
          for(int32_t j = 0; j < sizeof(configAttribs) / sizeof(EGLint) - 1; j += 2) {
            if(configAttribs[j] == EGL_NONE) {
              break;
            }

            if(configAttribs[j] == EGL_RED_SIZE || configAttribs[j] == EGL_GREEN_SIZE ||
               configAttribs[j] == EGL_BLUE_SIZE ||configAttribs[j] == EGL_ALPHA_SIZE) {
              auto value = EGLint{};
              eglGetConfigAttrib(_eglDisplay, c, configAttribs[j], &value);
              bitdiff += (value - configAttribs[j + 1]); // value is always >= configAttribs[j + 1]
            }
          }

          if(bitdiff < chosenBitdiff || chosenBitdiff == -1) {
            config = c;
            chosenBitdiff = bitdiff;
          }

          if(bitdiff == 0) {
            break; // exact match
          }
        }
      }

      auto monitor = Video::monitor(self.monitor);
      _monitorX = monitor.x;
      _monitorY = monitor.y;
      _monitorWidth = monitor.width;
      _monitorHeight = monitor.height;

      {
        XVisualInfo visual = {};
        {
          {
            EGLint visualid;
            if(!eglGetConfigAttrib(_eglDisplay, config, EGL_NATIVE_VISUAL_ID, &visualid)) {
              terminate();
              return false;
            }
            visual.visualid = visualid;
          }

          int visualsCount = 0;
          auto visuals = XGetVisualInfo(_xDisplay, VisualIDMask, &visual, &visualsCount);
          if(visualsCount < 1) {
            terminate();
            return false;
          }

          visual = visuals[0];
          XFree(visuals);
        }
        _xParent = self.fullScreen ? RootWindow(_xDisplay, visual.screen) : self.context;
        XWindowAttributes windowAttributes;
        XGetWindowAttributes(_xDisplay, _xParent, &windowAttributes);

        //(Window)self.context has already been realized, most likely with DefaultVisual.
        //GLX requires that the GL output window has the same Visual as the GLX context.
        //it is not possible to change the Visual of an already realized (created) window.
        //therefore a new child window, using the same GLX Visual, must be created and binded to it.
        _xColormap = XCreateColormap(_xDisplay, RootWindow(_xDisplay, visual.screen), visual.visual, AllocNone);
        XSetWindowAttributes attributes{};
        attributes.border_pixel = 0;
        attributes.colormap = _xColormap;
        attributes.override_redirect = self.fullScreen;
        _xWindow = XCreateWindow(_xDisplay, _xParent, 0, 0, windowAttributes.width, windowAttributes.height, 0,
                                visual.depth, InputOutput, visual.visual, CWBorderPixel | CWColormap | CWOverrideRedirect,
                                &attributes);
        XSelectInput(_xDisplay, _xWindow, ExposureMask);
        XSetWindowBackground(_xDisplay, _xWindow, 0);
        XMapWindow(_xDisplay, _xWindow);
        XFlush(_xDisplay);

        //window must be realized (appear onscreen) before we make the context current
        while(XPending(_xDisplay)) {
          XEvent event;
          XNextEvent(_xDisplay, &event);
        }
      }

      EGLint windowAttribs[] = {
        EGL_NONE
      };
      _eglSurface = eglCreateWindowSurface(_eglDisplay, config, (EGLNativeWindowType)_xWindow, windowAttribs);
      if(!_eglSurface) {
        terminate();
        return false;
      }

      if(!eglBindAPI(EGL_OPENGL_API)) {
        terminate();
        return false;
      }

      EGLint contextAttribs[] = {
        EGL_CONTEXT_MAJOR_VERSION, 3,
        EGL_CONTEXT_MINOR_VERSION, 2,
        EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
        EGL_NONE
      };
      _eglContext = eglCreateContext(_eglDisplay, config, EGL_NO_CONTEXT, contextAttribs);
      if(!_eglContext) {
        terminate();
        return false;
      }

      if(!eglMakeCurrent(_eglDisplay, _eglSurface, _eglSurface, _eglContext)) {
        terminate();
        return false;
      }

      if(!eglSwapInterval(_eglDisplay, self.blocking)) {
        terminate();
        return false;
      }

      EGLint attrib = 0;
      eglQueryContext(_eglDisplay, _eglContext, EGL_RENDER_BUFFER, &attrib);
      _doubleBuffer = (attrib != EGL_SINGLE_BUFFER);

      return _ready = OpenGL::initialize(self.shader);
    }

    auto terminate() -> void {
      _ready = false;
      OpenGL::terminate();

      if(_eglDisplay) {
        eglMakeCurrent(_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

        if(_eglContext) {
          eglDestroyContext(_eglDisplay, _eglContext);
          _eglContext = EGL_NO_CONTEXT;
        }

        if(_eglSurface) {
          eglDestroySurface(_eglDisplay, _eglSurface);
          _eglSurface = EGL_NO_SURFACE;
        }

        if(_xWindow) {
          XUnmapWindow(_xDisplay, _xWindow);
          _xWindow = 0;
        }

        if(_xColormap) {
          XFreeColormap(_xDisplay, _xColormap);
          _xColormap = 0;
        }
      }
    }

    bool _ready = false;

    Display* _xDisplay = nullptr;
    EGLDisplay _eglDisplay = EGL_NO_DISPLAY;
    EGLint _eglVersionMajor = 0, _eglVersionMinor = 0;
    EGLContext _eglContext = EGL_NO_CONTEXT;
    EGLSurface _eglSurface = EGL_NO_SURFACE;
    uint _monitorX = 0;
    uint _monitorY = 0;
    uint _monitorWidth = 0;
    uint _monitorHeight = 0;
    uint _depth = 24;  //depth of the default root window
    Window _xParent = 0;
    Window _xWindow = 0;
    Colormap _xColormap = 0;

    bool _doubleBuffer = false;
    bool _isDirect = false;
};
