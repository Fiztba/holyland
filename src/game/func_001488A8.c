int func_00192C98(void *, int, int, unsigned int, int);

typedef struct {
    char pad0[0xC];
    struct {
        char pad0[0xC4];
        unsigned char c4;
    } *c;
} Actor;

void func_001488A8(Actor *a) {
    func_00192C98(a, 0, 0, a->c->c4, 0);
}
