extern int D_004467B0[2];

void func_0011E608(char *arg0)
{
    unsigned char t;

    t = **(unsigned char **)(arg0 + 4);
    if ((((unsigned int)(t - 3) < 2U) || t == 0x15)
        && ((unsigned int)((*(int *)(arg0 + 8) - D_004467B0[*(unsigned char *)(arg0 + 0x10) & 1]) >> 7) >> 7) < 0x200U)
    {
        *(int *)(arg0 + 0x10) |= 0x40000;
    }
}
