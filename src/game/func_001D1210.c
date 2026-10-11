extern char D_00420D00[];
void func_00216C48(void *, void *, void *);

void func_001D1210(void *arg0) {
    char *temp;

    func_00216C48(D_00420D00, D_00420D00, arg0);
    temp = D_00420D00 + 0x20;
    func_00216C48(temp, temp, arg0);
}
