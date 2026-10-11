int func_00208168();
int func_00208198(int, int, int, int, int);

typedef struct
{
  int a00;
  int pad04;
  int a08;
  int pad0c;
  int a10;
  int pad14;
  int a18;
  int pad1c;
  int a20;
  int pad24;
  int a28;
  char pad2c[0x58];
  int a84;
} Obj;

void func_00212320(Obj *arg0)
{
  if (arg0->a84 == 0)
  {
    if (func_00208198(arg0->a08, arg0->a10, arg0->a18, arg0->a20, arg0->a28) != 0)
    {
      arg0->a84 = 1;
    }
  }
  else if (func_00208168() == 0)
  {
    arg0->a00 = 0;
  }
}
