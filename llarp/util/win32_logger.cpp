#if defined(_WIN32)
#include <util/win32_logger.hpp>
#include <util/logger_internal.hpp>

namespace llarp
{
  Win32LogStream::Win32LogStream(std::ostream& out) : OStreamLogStream(out)
  {
    // Attempt to use ANSI escapes directly
    // if the modern console is active.
    DWORD mode_flags;

    GetConsoleMode(fd1, &mode_flags);
    // since release SDKs don't have ANSI escape support yet
    // we get all or nothing: if we can't get it, then we wouldn't
    // be able to get any of them individually
    mode_flags |= 0x0004 | 0x0008;
    BOOL t = SetConsoleMode(fd1, mode_flags);
    if(!t)
      this->isConsoleModern = false;  // fall back to setting colours manually
  }

  void
  Win32LogStream::PreLog(std::stringstream& ss, LogLevel lvl, const char* fname,
                         int lineno) const
  {
    if(!isConsoleModern)
    {
      switch(lvl)
      {
        case eLogNone:
          break;
        case eLogDebug:
          ss << "[DBG] ";
          break;
        case eLogInfo:
          ss << "[NFO] ";
          break;
        case eLogWarn:
          ss << "[WRN] ";
          break;
        case eLogError:
          ss << "[ERR] ";
          break;
      }
      ss << "(" << thread_id_string() << ") " << log_timestamp() << " " << fname
         << ":" << lineno << "\t";
    }
    else
      OStreamLogStream::PreLog(ss, lvl, fname, lineno);
  }

  void
  Win32LogStream::PostLog(std::stringstream& ss) const
  {
    if(isConsoleModern)
      OStreamLogStream::PostLog(ss);
    else
      ss << std::endl;
  }
}  // namespace llarp
#endif