typedef struct {
  int a00;
  int a04;
  int a08;
  int a0c;
  int a10;
  int a14;
  int a18;
  int a1c;
  int a20;
  int a24;
  int a28;
} Obj;

typedef struct {
  char pad[0x7C];
  void *a7c;
} Actor;

extern int D_00446770;
void *func_00116160(int, int);
void func_0010E8E0(void *, int, int, int, int);

void func_00213C98(Obj *arg0) {
  Actor *temp_v0;

  temp_v0 = func_00116160(D_00446770, arg0->a08);
  if (temp_v0 != 0) {
    func_0010E8E0(temp_v0->a7c, arg0->a10, arg0->a18, arg0->a20, arg0->a28);
  }
  arg0->a00 = 0;
}
