#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <dirent.h>

#include <json.h>

#define JSON_GET(obj, args...) _json_get(obj, ## args, NULL)

#define JSON_ARRAY_FOREACH(obj, val) \
  int max = json_object_array_length(obj), idx; \
  json_object *val; \
  for (idx = 0; idx < max ? \
    ( val = json_object_array_get_idx(obj, idx), 1) : 0; idx++)

#define STRING_GET(obj) \
  ((obj && json_object_get_type(obj) == json_type_string) ? \
   json_object_get_string(obj) : NULL)

#define ERR printf

typedef struct _List
{
  struct _List *next;
  void *data;
} List;

typedef struct
{
  const char *name;
  const char *path;
  List *dependents; /* List of Repo which depend on this repo */
  List *builds; /* List of const char * */
  int valid;
  int todo;
} Repo;

static List *repos = NULL; /* List of Repo */

static json_object *
_json_get(json_object *obj, ...)
{
  char* jkey;
  va_list vl;
  json_object *jval;

  if (!obj) return NULL;

  jval = obj;
  va_start(vl, obj);
  while ((jkey = va_arg(vl, char*)))
  {
    struct json_object *jtmp = NULL;
    if (!json_object_object_get_ex(jval, jkey, &jtmp) || !jtmp) return NULL;
    jval = jtmp;
  }
  va_end(vl);

  if (jval == obj) jval = NULL;

  return jval;
}

static char*
_file_get_as_string(const char *filename)
{
  char *file_data = NULL;
  int file_size;
  FILE *fp = fopen(filename, "r");
  if (!fp)
  {
    ERR("Can not open file: \"%s\".", filename);
    return NULL;
  }

  fseek(fp, 0, SEEK_END);
  file_size = ftell(fp);
  if (file_size == -1)
  {
    fclose(fp);
    ERR("Can not ftell file: \"%s\".", filename);
    return NULL;
  }
  rewind(fp);
  file_data = (char *) calloc(1, file_size + 1);
  if (!file_data)
  {
    fclose(fp);
    ERR("Calloc failed");
    return NULL;
  }
  int res = fread(file_data, file_size, 1, fp);
  fclose(fp);
  if (!res)
  {
    free(file_data);
    file_data = NULL;
    ERR("fread failed");
  }
  return file_data;
}

static List *
_list_last_get(List *l)
{
  if (!l) return NULL;
  while (l->next) l = l->next;
  return l;
}

static List *
_list_append(List *l, void *data)
{
  List *last = _list_last_get(l);
  List *e = calloc(1, sizeof(*e));
  e->data = data;
  if (last) last->next = e;
  else l = e;
  return l;
}

static Repo *
_repo_create(const char *name)
{
  Repo *r;
  List *e = repos;
  while (e)
  {
    r = e->data;
    if (!strcmp(r->name, name)) return r;
    e = e->next;
  }

  r = calloc(1, sizeof(*r));
  r->name = name;
  repos = _list_append(repos, r);
  return r;
}

static int
_git_last_id(Repo *r, char *output)
{
  char buf[256];
  FILE *fp;
  sprintf(buf, "cd %s; git log -1 --pretty=format:'%%h'", r->path);
  fp = popen(buf, "r");
  if (!fp)
  {
    return 1;
  }
  fread(output, 1, 10, fp);
  pclose(fp);
  return 0;
}

static void
_set_repo_as_todo(Repo *r)
{
  List *e = r->dependents;
  r->todo = 1;
  printf("Repository %s to check\n", r->name);
  while (e)
  {
    r = e->data;
    _set_repo_as_todo(r);
    e = e->next;
  }
}

int main(int argc, char **argv)
{
  char buf[1024];
  struct dirent *ep;
  Repo *r;
  List *e;
  int ret = 1;

  (void) argc;
  (void) argv;

  DIR *dp = opendir("./configs");
  if (!dp)
  {
    perror("opendir");
    goto end;
  }

  system("date");
  /* Open configs and build dependency tree */
  while ((ep = readdir (dp)))
  {
    if (!strcmp(ep->d_name + strlen(ep->d_name) - 5, ".json"))
    {
      json_object *arr_obj;
      sprintf(buf, "./configs/%s", ep->d_name);
      char *jbuf = _file_get_as_string(buf);
      json_object *jobj = json_tokener_parse(jbuf);
      r = _repo_create(STRING_GET(JSON_GET(jobj, "name")));
      r->path = STRING_GET(JSON_GET(jobj, "path"));
      r->valid = 1;

      arr_obj = JSON_GET(jobj, "depends");
      if (json_object_get_type(arr_obj) == json_type_array)
      {
        JSON_ARRAY_FOREACH(arr_obj, dep)
        {
          Repo *rd = _repo_create(STRING_GET(dep));
          rd->dependents = _list_append(rd->dependents, r);
        }
      }

      arr_obj = JSON_GET(jobj, "builds");
      if (json_object_get_type(arr_obj) == json_type_array)
      {
        JSON_ARRAY_FOREACH(arr_obj, bline)
        {
          r->builds = _list_append(r->builds, (void *)STRING_GET(bline));
        }
      }
    }

  }
  closedir (dp);

  /* Pull code and check if changes happened */
  e = repos;
  while (e)
  {
    r = e->data;
    if (r->valid)
    {
      char old_id[20], new_id[20];
      memset(old_id, 0, sizeof(old_id));
      memset(new_id, 0, sizeof(new_id));
      sprintf(buf, "cd %s; git checkout master > /dev/null 2>&1", r->path);
      if (system(buf) != 0)
      {
        fprintf(stderr, "Unable to move to master branch of repo %s\n", r->name);
        goto end;
      }
      if (_git_last_id(r, old_id) != 0)
      {
        fprintf(stderr, "Unable to git information from repo %s\n", r->name);
        goto end;
      }
      sprintf(buf, "cd %s; git pull --rebase --recurse-submodules=yes > /dev/null 2>&1", r->path);
      if (system(buf) != 0)
      {
        fprintf(stderr, "Unable to pull master branch of repo %s\n", r->name);
        goto end;
      }
      if (_git_last_id(r, new_id) != 0)
      {
        fprintf(stderr, "Unable to git information from repo %s\n", r->name);
        goto end;
      }
      if (strcmp(old_id, new_id)) _set_repo_as_todo(r);
    }
    e = e->next;
  }

  /* Build the repos that changed or whose dependencies changed */
  e = repos;
  while (e)
  {
    r = e->data;
    if (r->todo == 1)
    {
      List *builds = r->builds;
      int build_id = 1;
      while (builds)
      {
        const char *bline = builds->data;
        printf("Check repo %s - Build %d\n", r->name, build_id);
        sprintf(buf, "echo \"cd %s; %s\" > /tmp/auto_check.sh", r->path, bline);
        system(buf);
        system("cat /tmp/auto_check.sh > /tmp/auto_check.log");
        if (system("sh /tmp/auto_check.sh >> /tmp/auto_check.log 2>&1") != 0)
        {
          printf("Build %d of repo %s failed\n", build_id, r->name);
          if (access("./mailrc", R_OK) == 0)
          {
            sprintf(buf, "cat /tmp/auto_check.log | MAILRC=./mailrc s-nail -s \"%s: build %d failed\" destination", r->name, build_id);
            system(buf);
          }
          else printf("mailrc file not found in current directory\n");
        }
        else printf("Build %d of repo %s succeeded\n", build_id, r->name);
        builds = builds->next;
        build_id++;
      }
    }
//    printf("Repo %s\n", r->name);
    e = e->next;
  }

  ret = 0;
end:
  return ret;
}