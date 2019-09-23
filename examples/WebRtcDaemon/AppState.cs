//-----------------------------------------------------------------------------
// Filename: AppState.cs
//
// Description: The AppState class holds static application configuration settings for 
// objects requiring configuration information. AppState provides a one stop
// shop for settings rather then have configuration functions in separate 
// classes.
//
// History:
// 23 Sep 2019	Aaron Clauson	Created.
//
// License: 
// This software is licensed under the BSD License http://www.opensource.org/licenses/bsd-license.php
//
// Copyright (c) 2019 Aaron Clauson (aaron@sipsorcery.com), SIP Sorcery Pty Ltd, Montreux, Switzerland (www.sipsorcery.com)
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without modification, are permitted provided that 
// the following conditions are met:
//
// Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer. 
// Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following 
// disclaimer in the documentation and/or other materials provided with the distribution. Neither the name of SIP Sorcery Pty Ltd 
// nor the names of its contributors may be used to endorse or promote products derived from this software without specific 
// prior written permission. 
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, 
// BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
// IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, 
// OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, 
// OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, 
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE 
// POSSIBILITY OF SUCH DAMAGE.
//-----------------------------------------------------------------------------

using System;
using System.Collections.Specialized;
using System.Configuration;
using System.IO;
using System.Reflection;
using log4net;

namespace SIPSorcery.Net.WebRtc
{
    public class AppState
    {
        public const string DEFAULT_ERRRORLOG_FILE = @"c:\temp\appstate.error.log";
        private const string APP_LOGGING_ID = "sipsorcery";     // Name of log4net identifier.

        public static ILog logger;		                          // Used to provide logging functionality for the application.

        private static StringDictionary m_appConfigSettings;	  // Contains application configuration key, value pairs.

        static AppState()
        {
            try
            {
                try
                {
                    // Initialise logging functionality from an XML node in the app.config file.
                    Console.WriteLine("Starting logging initialisation.");

                    // dotnet core doesn't have app.config or web.config so the default log4net config initialisation cannot be used.
                    // The alternative is to use a dedicated log4net.config file which can contain exactly the same block of XML.
                    log4net.Config.XmlConfigurator.Configure();
                }
                catch
                {
                    // Unable to load the log4net configuration node (probably invalid XML in the config file).
                    Console.WriteLine("Unable to load logging configuration check that the app.config file exists and is well formed.");

                    try
                    {
                        //EventLog.WriteEntry(APP_LOGGING_ID, "Unable to load logging configuration check that the app.config file exists and is well formed.", EventLogEntryType.Error, 0);
                    }
                    catch (Exception evtLogExcp)
                    {
                        Console.WriteLine("Exception writing logging configuration error to event log. " + evtLogExcp.Message);
                    }

                    // Configure a basic console appender so if there is anyone watching they can still see log messages and to
                    // ensure that any classes using the logger won't get null references.
                    ConfigureConsoleLogger();
                }
                finally
                {
                    try
                    {
                        //logger = log4net.LogManager.GetLogger(APP_LOGGING_ID); // .Net framework call.
                        logger = log4net.LogManager.GetLogger(Assembly.GetEntryAssembly(), APP_LOGGING_ID);
                        logger.Debug("Logging initialised.");
                    }
                    catch (Exception excp)
                    {
                        StreamWriter errorLog = new StreamWriter(DEFAULT_ERRRORLOG_FILE, true);
                        errorLog.WriteLine(DateTime.Now.ToString("dd MMM yyyy HH:mm:ss") + " Exception Initialising AppState Logging. " + excp.Message);
                        errorLog.Close();
                    }
                }

                // Initialise the string dictionary to hold the application settings.
                m_appConfigSettings = new StringDictionary();
            }
            catch (Exception excp)
            {
                StreamWriter errorLog = new StreamWriter(DEFAULT_ERRRORLOG_FILE, true);
                errorLog.WriteLine(DateTime.Now.ToString("dd MMM yyyy HH:mm:ss") + " Exception Initialising AppState. " + excp.Message);
                errorLog.Close();
            }
        }

        public static ILog GetLogger(string logName)
        {
            return log4net.LogManager.GetLogger(logName); // .net framework call.
        }

        /// <summary>
        /// Configures the logging object to use a console logger. This would normally be used
        /// as a fallback when either the application does not have any logging configuration
        /// or there is an error in it.
        /// </summary>
        public static void ConfigureConsoleLogger()
        {
            log4net.Appender.ConsoleAppender appender = new log4net.Appender.ConsoleAppender();

            log4net.Layout.ILayout fallbackLayout = new log4net.Layout.PatternLayout("%m%n");
            appender.Layout = fallbackLayout;
            log4net.Config.BasicConfigurator.Configure(appender);
        }

        /// <summary>
        /// Wrapper around the object holding the application configuration settings extracted
        /// from the App.Config file.
        /// </summary>
        /// <param name="key">The name of the configuration setting wanted.</param>
        /// <returns>The value of the configuration setting.</returns>
        public static string GetConfigSetting(string key)
        {
            try
            {
                if (m_appConfigSettings != null && m_appConfigSettings.ContainsKey(key))
                {
                    return m_appConfigSettings[key];
                }
                else
                {
                    string setting = ConfigurationManager.AppSettings[key];

                    if (!String.IsNullOrEmpty(setting))
                    {
                        m_appConfigSettings[key] = setting;
                        return setting;
                    }
                    else
                    {
                        return null;
                    }
                }
            }
            catch (Exception excp)
            {
                logger.Error("Exception AppState.GetSetting. " + excp.Message);
                throw;
            }
        }
    }
}
