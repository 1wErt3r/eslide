#include "common.h"

// Global logging domain definition
int _log_domain = -1;

// Function to initialize logging domain
int common_init_logging(void)
{
   _log_domain = eina_log_domain_register("efl-hello", EINA_COLOR_BLUE);
   if (_log_domain < 0)
   {
      EINA_LOG_CRIT("Could not register log domain: efl-hello");
      return -1;
   }
   
   INF("Logging domain initialized");
   return 0;
}

// Function to cleanup logging domain
void common_cleanup_logging(void)
{
   if (_log_domain >= 0)
   {
      eina_log_domain_unregister(_log_domain);
      _log_domain = -1;
   }
}