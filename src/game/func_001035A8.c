
extern unsigned char D_00354928[];
void func_001035A8(unsigned int x, int y)
{
  unsigned int t;
  unsigned int i;
  unsigned int s;
  unsigned int c;
  int new_var;
  if (((x < 0x80) && (y >= 0)) && (y < 0x80))
  {
    c = (i = D_00354928[(y / 2) + (x << 6)]);
    if (y & 1)
    {
      c = c >> 4;
      c++;
      t = c << 4;
      D_00354928[(y / 2) + (x << 6)] = (s = i & 0xF);
      c = t & 0xFF;
    }
    else
    {
      c &= 0xF;
      c++;
      D_00354928[(y / 2) + (x << 6)] = i & 0xF0;
    }
    new_var = c | D_00354928[(y / 2) + (x << 6)];
    D_00354928[(y / 2) + (x << 6)] = new_var;
  }
}
