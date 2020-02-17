//-----------------------------------------------------------------------------
// Filename: Program.cs
//
// Description: Main entry point to start the application.
//
// Author(s):
// Aaron Clauson (aaron@sipsorcery.com)
//
// History:
// 04 Mar 2016	Aaron Clauson	Created, Hobart, Australia.
//
// License: 
// BSD 3-Clause "New" or "Revised" License, see included LICENSE.md file.
//-----------------------------------------------------------------------------

using System;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;

namespace SIPSorcery.Net.WebRtc
{
    class Program
    {
        static void Main()
        {
            try
            {
                //Windows service has system32 as default working folder, we change the working dir to install dir for file access
                System.IO.Directory.SetCurrentDirectory(System.AppDomain.CurrentDomain.BaseDirectory);

                Host.CreateDefaultBuilder()
                    .ConfigureServices((hostContext, services) =>
                    {
                        services.AddHostedService<WebRtcWorker>();
                    }).UseWindowsService().Build().Run();
            }
            catch (Exception excp)
            {
                Console.WriteLine("Exception Main. " + excp);
            }
        }
    }
}
