/* Copyright 2012 Kjetil S. Matheussen

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA. */

#include "Python.h"


#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "../common/nsmtracker.h"
#include "../common/OS_settings_proc.h"

char *OS_get_program_path(void){
  static char *program_path = NULL;
  if(program_path!=NULL){
    return program_path;
  }

#ifdef FOR_WINDOWS

  int size=128;
  char *ret;
  do{
    size *= 2;
    ret=talloc_atomic(size);
  }while(getcwd(ret,size-1)==NULL);

  program_path = ret;

  return ret;

#else

  PyObject* sys_module_string = PyString_FromString((char*)"sys");
  PyObject* sys_module = PyImport_Import(sys_module_string);
  PyObject* g_program_path = PyObject_GetAttrString(sys_module,(char*)"g_program_path");

  program_path = talloc_strdup(PyString_AsString(g_program_path));
  return program_path;

#endif
}

#if 0
// This function is moved to Qt/Qt_settings.c
char *OS_get_config_filename(void){
#if __linux__ || defined(FOR_MACOSX)
  char temp[500];
  sprintf(temp,"mkdir %s/.radium 2>/dev/null",getenv("HOME"));
  if(system(temp)==-1)
    RWarning("Unable to create \"%s/.radium\" directory",getenv("HOME"));

  sprintf(temp,"%s/.radium/config",getenv("HOME"));
  return talloc_strdup(temp);
#endif // __linux__

#if FOR_WINDOWS
  return talloc_strdup("config");
#endif
}
#endif

void OS_make_config_file_expired(const char *key){
  char *config_file = OS_get_config_filename(key);
  char temp[500];
  sprintf(temp,"%s_bu",config_file);
  rename(config_file,temp);
}
