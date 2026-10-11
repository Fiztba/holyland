extern int D_0043EE30[];

void func_001A0CD0(int arg0, int arg1, int arg2) {
  int (*fn)(int, int);
  fn = *((int (**)(int, int)) (((char *) D_0043EE30) + ((arg1 & 0xFF) * 4)));
  fn(arg0 & 0xFF, arg2);
}
