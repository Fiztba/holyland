#include "kf_script.h"
#include "kf_splash.h"

void func_002120E0(ScriptWaitCommand *command)
{
  if (command->started == 0)
  {
    if (KF_Splash_Start(command->object_id.value) != 0)
    {
      command->started = 1;
    }
  }
  else if (KF_Splash_GetState() == 0)
  {
    command->active = 0;
  }
}
