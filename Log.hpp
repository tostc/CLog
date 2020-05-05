/*
 * MIT License
 *
 * Copyright (c) 2020 Christian Tost
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef LOG_HPP
#define LOG_HPP

#include <string>
#include <type_traits>
#include <functional>
#include <iostream>
#include <fstream>
#include <mutex>
#include <thread>
#include <map>
#include <time.h>

/** 
 * Simple logging class with multithreading support. 
 */
class CLog
{
    public:
        /** Fileinterface **/
        class IFileOut
        {
            public:
                /**
                 * @brief Called if a new file should be open.
                 * 
                 * @param Filename: Name of the log file.
                 */
                virtual void OpenFile(const std::string &Filename) = 0;

                /**
                 * @brief Called if a message is flushed to the file.
                 * 
                 * @param Message: Message to write.
                 */
                virtual void WriteToFile(const std::string &Message) = 0;

                /**
                 * @brief Called if the file should be close. 
                 */
                virtual void CloseFile() = 0;
        };

        /** Messagebuffer **/
        struct Buffer
        {
            Buffer()
            {
                Level = 0;
                DebugMsg = false;
                ShowTime = true;
                Time = time(nullptr);
            }

            time_t Time;            //!< Timestamp when this buffer was created.
            std::string Tag;        //!< Tag for the message. "error", "info", "warning", "debug" or a custom tag.
            std::string Message;    //!< Formatted outputmessage.
            uint32_t Level;         //!< Loglevel of this message.
            bool DebugMsg;          //!< True if debug message. Only visible if the log has debug enabled. @see CLog::EnableDebugLog
            bool ShowTime;          //!< True if the time should be shown.
        };

        CLog() 
        {
            m_Output = std::bind(&CLog::ConsoleOut, this, std::placeholders::_1);
            m_OutFile = std::shared_ptr<IFileOut>(new DefaultFileOut());
            m_FormatMessage = std::bind(&CLog::FormatMessage, this, std::placeholders::_1);
            m_LogToFile = false;
            m_LogDebug = false;
            m_Format = 'd';
            m_Level = -1;
        }

        /**
         * @brief Opens a file for logging.
         * 
         * You can enable with this function the file logging. All the log messages
         * will also be written into this file.
         * 
         * @param Filename: Logfile name
         * 
         * @throw May throw an exception if file opening failed. 
         */
        inline void LogToFile(const std::string &Filename)
        {
            CloseLogFile();

            std::lock_guard<std::mutex> lock(m_LogLock);
            m_OutFile->OpenFile(Filename);
            m_LogToFile = true;
        }

        /**
         * @brief Closes the logfile.
         * 
         * Closes the open log file. After that the messages 
         * won't be written anymore to file. Has no effect if the file is 
         * already closed.
         */
        inline void CloseLogFile()
        {
            std::lock_guard<std::mutex> lock(m_LogLock);
            m_LogToFile = false;
            m_OutFile->CloseFile();
        }

        /**
         * @brief Input for signed integrals, which are non-pointers.
         * 
         * Converts the values to strings and considered the format settings. For format settings see @see ::lbin,
         * @see ::lhex, @see ::ldec.
         * 
         * @param val: Integral value.
         */
        template<class T, typename std::enable_if<std::is_integral<T>::value && !std::is_unsigned<T>::value && !std::is_same<T, bool>::value && !std::is_pointer<T>::value>::type* = nullptr>
        inline CLog &operator<<(const T &val)
        {
            std::lock_guard<std::mutex> lock(m_LogLock);
            m_Buffers[std::this_thread::get_id()].Message += FormatIntegral((long)val);
            return *this;
        }

        /**
         * @brief Input for unsigned integrals, which are non-pointers.
         * 
         * @param val: Integral value.
         */
        template<class T, typename std::enable_if<std::is_integral<T>::value && std::is_unsigned<T>::value && !std::is_same<T, bool>::value && !std::is_pointer<T>::value>::type* = nullptr>
        inline CLog &operator<<(const T &val)
        {
            std::lock_guard<std::mutex> lock(m_LogLock);
            m_Buffers[std::this_thread::get_id()].Message += FormatIntegral((unsigned long)val);
            return *this;
        }

        /**
         * @brief Input for floating point values, which are non-pointers.
         * 
         * @param val: floating point value.
         */
        template<class T, typename std::enable_if<std::is_floating_point<T>::value && !std::is_pointer<T>::value>::type* = nullptr>
        inline CLog &operator<<(const T &val)
        {
            std::lock_guard<std::mutex> lock(m_LogLock);
            m_Buffers[std::this_thread::get_id()].Message += std::to_string(val);
            return *this;
        }

        /**
         * @brief Input for bool values, which are non-pointers.
         * 
         * Converts the boolean value to "true" or "false" and append them to the message.
         * 
         * @param val: bool value.
         */
        inline CLog &operator<<(bool val)
        {
            std::lock_guard<std::mutex> lock(m_LogLock);
            if(val)
                m_Buffers[std::this_thread::get_id()].Message += "true";
            else
                m_Buffers[std::this_thread::get_id()].Message += "false";

            return *this;
        }

        /**
         * @brief Input string value and append them to the message.
         * 
         * @param val: string value.
         */
        inline CLog &operator<<(const std::string &val)
        {
            std::lock_guard<std::mutex> lock(m_LogLock);
            m_Buffers[std::this_thread::get_id()].Message += val;
            return *this;
        }

        /**
         * @brief Input string value and append them to the message.
         * 
         * @param val: string value.
         */
        inline CLog &operator<<(const char *val)
        {
            return operator<<(std::string(val));
        }

        /**
         * @brief Calls a given function.
         * 
         * @param fun: Function to call.
         */
        inline CLog &operator<<(CLog& (*fun)(CLog&))
        {
            return fun(*this);
        }

        /**
         * @brief Flushes the message queue of the caller thread.
         * 
         * Flushes the message queue of the caller thread prints and writes the message.
         */
        inline void Flush()
        {
            std::lock_guard<std::mutex> lock(m_LogLock);
            Buffer buf = m_Buffers[std::this_thread::get_id()];

            if((m_LogDebug || (!m_LogDebug && !buf.DebugMsg)) && buf.Level <= m_Level)
            {
                std::string Message = m_FormatMessage(buf);

                m_Output(Message);
                if(m_LogToFile)
                    m_OutFile->WriteToFile(Message);
            }

            m_Buffers.erase(std::this_thread::get_id());
        }

        /**
         * @brief Flushes the message queue of the all threads.
         * 
         * Flushes the message queue of the alls threads prints and writes the message.
         */
        inline void FlushAll()
        {
            std::lock_guard<std::mutex> lock(m_LogLock);
            std::map<std::thread::id, Buffer>::iterator IT = m_Buffers.begin();
            while (IT != m_Buffers.end())
            {
                if((m_LogDebug || (!m_LogDebug && !IT->second.DebugMsg)) && IT->second.Level <= m_Level)
                {
                    std::string Message = m_FormatMessage(IT->second);

                    m_Output(Message);
                    if(m_LogToFile)
                        m_OutFile->WriteToFile(Message);
                }

                IT = m_Buffers.erase(IT);
            }
        }

        /**
         * @brief Adds a letter to the message queue.
         * 
         * @param c: Char to add.
         */
        inline void Put(const char &c)
        {
            std::lock_guard<std::mutex> lock(m_LogLock);
            m_Buffers[std::this_thread::get_id()].Message += c;
        }

        /**
         * @brief Sets the tag for the current message.
         * 
         * If the tag name is "debug", the message is only visible if
         * @see CLog::EnableDebugLog is set to true.
         * 
         * @param Tag: Tag name. You can set four default tags. @see ::linfo, @see ::lwarning, @see ::lerror, @see ::ldebug and
         * @see ::ltag.
         */
        inline void SetTag(const std::string &Tag)
        {
            std::lock_guard<std::mutex> lock(m_LogLock);
            m_Buffers[std::this_thread::get_id()].Tag = Tag;
            m_Buffers[std::this_thread::get_id()].DebugMsg = Tag == "debug";
        }

        /**
         * @brief Sets the loglevel for the current thread message.
         */
        inline void SetLogLevel(uint32_t Level)
        {
            std::lock_guard<std::mutex> lock(m_LogLock);
            m_Buffers[std::this_thread::get_id()].Level = Level;
        }

        /**
         * @brief Sets the time for the current buffer visible or invisible.
         */
        inline void ShowTimestamp(bool Show)
        {
            std::lock_guard<std::mutex> lock(m_LogLock);
            m_Buffers[std::this_thread::get_id()].ShowTime = Show;
        }

        /**
         * @brief Show only logs up to and including level.
         * 
         * @param Level: -1 means all levels.
         */
        inline void ShowLogLevel(uint32_t Level)
        {
            m_Level = Level;
        }
        
        /**
         * @brief Enables debug messages.
         * 
         * @param State: True if the debug messages should be print.
         */
        inline void EnableDebugLog(bool State)
        {
            std::lock_guard<std::mutex> lock(m_LogLock);
            m_LogDebug = State;
        }

        /**
         * @brief Sets the format type how numbers will be present.
         * 
         * Possible format types are 'x' for hex, 'd' for decimal, 'b' for binary and 'o' for octal.
         */
        inline void SetNumFormat(char format)
        {
            m_Format = format;
        }

        /**
         * @brief Sets the callback which will print the message to screen.
         * 
         * This callback can also be a method of a class which is created via std::bind.
         * 
         * @param Output: The new output callback.
         */
        inline void SetOutputCallback(std::function<void(const std::string&)> Output)
        {
            m_Output = Output;
        }

        /**
         * @brief Sets the callback which will format a message in its final form.
         * 
         * This callback can also be a method of a class which is created via std::bind.
         * 
         * @param FormatMessage: The new format callback.
         */
        inline void SetFormatCallback(std::function<std::string(const Buffer&)> FormatMessage)
        {
            m_FormatMessage = FormatMessage;
        }

        /**
         * @brief Sets the object which will handle file operations.
         * 
         * This method will override the standard file output of this class. For more infos see IFileOut.
         */
        inline void SetFileOutCallback(std::shared_ptr<IFileOut> FileOut)
        {
            m_OutFile = FileOut;
        }

        /**
         * @return Gets a reference to the mutex of the log class. This should be used if you implement own callbacks like output, format or file operations.
         */
        inline std::mutex &GetLock()
        {
            return m_LogLock;
        }

        ~CLog() 
        {
            FlushAll();
            CloseLogFile();
        }

    private:
        // Default file interface.
        class DefaultFileOut : public IFileOut
        {
            public:
                void OpenFile(const std::string &Filename)
                {
                    m_Output.open(Filename, std::ios::out);
                }

                void WriteToFile(const std::string &Message)
                {
                    m_Output << Message;
                    m_Output.flush();
                }

                void CloseFile()
                {
                    if(m_Output.is_open())
                        m_Output.close();
                }

                virtual ~DefaultFileOut()
                {
                    CloseFile();
                }
            private:
                std::ofstream m_Output;
        };

        std::function<void(const std::string&)> m_Output;
        std::function<std::string(const Buffer&)> m_FormatMessage;
        std::shared_ptr<IFileOut> m_OutFile;

        bool m_LogToFile;
        bool m_LogDebug;
        std::map<std::thread::id, Buffer> m_Buffers;
        std::mutex m_LogLock;
        char m_Format;
        uint32_t m_Level;

        inline std::string FormatIntegral(long Num, std::string Unsigned)
        {
            std::string Ret;

            if(m_Format != 'b')
            {
                std::string Format = "%l";

                if(m_Format == 'd')
                    Format += Unsigned;
                else
                    Format += m_Format;

                int Size = snprintf(nullptr, 0, Format.c_str(), Num);
                Ret.resize(Size + 1, 0);
                snprintf(&Ret[0], Size + 1, Format.c_str(), Num);
            }
            else  // 'Converts' the number to binary.
            {
                bool FirstOneFound = false;
                for (char i = sizeof(long); i--;)
                {
                    if(!FirstOneFound && (Num & (1 << i)))
                        FirstOneFound = true;
                    
                    if(FirstOneFound)
                        Ret += (Num & (1 << i)) ? '1' : '0';
                }
                
                if(Ret.empty())
                    Ret = "0";
            }

            return Ret;
        }

        inline std::string FormatIntegral(unsigned long Num)
        {
            return FormatIntegral(Num, "u");
        }

        inline std::string FormatIntegral(long Num)
        {
            return FormatIntegral(Num, "d");
        }

        // Default output.
        inline void ConsoleOut(const std::string& out)
        {
            std::cout << out;
            std::cout.flush();
        }

        // Default format.
        inline std::string FormatMessage(const Buffer &buf)
        {
            std::string Ret;

            for(uint32_t i = 0; i < buf.Level; i++)
                Ret += ' ';

            if(buf.ShowTime)
            {    
                struct tm *timeinfo = localtime(&buf.Time);
                char Buf[80];
                strftime(Buf, sizeof(Buf), "%F %H:%M:%S", timeinfo);

                Ret += "[ " + std::string(Buf) + " ] ";
            }

            if(!buf.Tag.empty())
                Ret += "[ " + buf.Tag + " ] ";

            Ret += buf.Message;

            return Ret;
        }
};

extern CLog llog;

/* Wrapper for the loglevel. */
struct LogLevel
{
    LogLevel(uint32_t level) : m_Level(level) { }

    friend CLog &operator<<(CLog &log, const LogLevel &loglvl)
    {
        log.SetLogLevel(loglvl.m_Level);
        return log;
    }

    private:
        uint32_t m_Level;
};

/* Wrapper for tags. */
struct CustomTag
{
    CustomTag(const std::string &CustomTag) : m_CustomTag(CustomTag) { }

    friend CLog &operator<<(CLog &log, const CustomTag &customTag)
    {
        log.SetTag(customTag.m_CustomTag);
        return log;
    }

    protected:
        std::string m_CustomTag;
};

/**
 * @brief Shows a custom tag.
 * 
 * @param Tag: The tag name.
 * 
 * @return Wrapper which sets the tag.
 */
inline CustomTag ltag(const std::string &Tag)
{
    return CustomTag(Tag);
}

/**
 * @brief Flushes a log message and add a newline to the output.
 */
inline CLog &lendl(CLog &log)
{
    log.Put('\n');
    log.Flush();

    return log;
}

/**
 * @brief All numbers will be present as hex digits.
 */
inline CLog &lhex(CLog &log)
{
    log.SetNumFormat('x');
    return log;
}

/**
 * @brief All numbers will be present as decimal digits.
 * 
 * This is the default number format.
 */
inline CLog &ldec(CLog &log)
{
    log.SetNumFormat('d');
    return log;
}

/**
 * @brief All numbers will be present as binary digits.
 */
inline CLog &lbin(CLog &log)
{
    log.SetNumFormat('b');
    return log;
}

/**
 * @brief All numbers will be present as octal digits.
 */
inline CLog &loct(CLog &log)
{
    log.SetNumFormat('o');
    return log;
}

/**
 * @brief Sets the tag of this message to "error".
 */
inline CLog &lerror(CLog &log)
{
    log.SetTag("error");
    return log;
}

/**
 * @brief Sets the tag of this message to "warning".
 */
inline CLog &lwarning(CLog &log)
{
    log.SetTag("warning");
    return log;
}

/**
 * @brief Sets the tag of this message to "info".
 */
inline CLog &linfo(CLog &log)
{
    log.SetTag("info");
    return log;
}

/**
 * @brief Sets the tag of this message to "debug".
 * 
 * Debug messages can be disabled with CLog::EnableDebugLog.
 */
inline CLog &ldebug(CLog &log)
{
    log.SetTag("debug");
    return log;
}

/**
 * @brief Sets the log level of a message.
 * 
 * @return Wrapper which sets the log level.
 */
inline LogLevel llevel(uint32_t Level)
{
    return LogLevel(Level);
}

#endif //LOG_HPP
#ifdef CLOG_IMPLEMENTATION
    CLog llog;
#endif