//-----------------------------------------------------------------------------
// Filename: WebRtcWorker.cs
//
// Description: Long running background worker to control the daemon.
//
// Author(s):
// Aaron Clauson (aaron@sipsorcery.com)
//
// History:
// 04 Mar 2016	Aaron Clauson	Created, Hobart, Australia.
// 17 Feb 2020  Aaron Clauson   Switched from net472 service to netcoreapp3.1 
//                               background worker.
//
// License: 
// BSD 3-Clause "New" or "Revised" License, see included LICENSE.md file.
//-----------------------------------------------------------------------------

using System.Threading;
using System.Threading.Tasks;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;

namespace SIPSorcery.Net.WebRtc
{
    public class WebRtcWorker : BackgroundService
    {
        private readonly ILogger<WebRtcWorker> _logger;

        private readonly WebRtcDaemon _daemon;

        public WebRtcWorker(ILogger<WebRtcWorker> logger)
        {
            _logger = logger;
            _daemon = new WebRtcDaemon();
        }

        protected override Task ExecuteAsync(CancellationToken ct)
        {
            return _daemon.Start(ct);
        }
    }
}
