#include <cstdio>
#include <cstring>
#include <rw.h>
using namespace rw;

static void printmat(const char *tag, const Matrix &m){
    printf("%s right %.6f %.6f %.6f up %.6f %.6f %.6f at %.6f %.6f %.6f pos %.6f %.6f %.6f\n",
        tag, m.right.x,m.right.y,m.right.z, m.up.x,m.up.y,m.up.z,
        m.at.x,m.at.y,m.at.z, m.pos.x,m.pos.y,m.pos.z);
}

int main(int argc, char **argv){
    if(argc<2){fprintf(stderr,"usage: %s file.dff\n",argv[0]);return 1;}
    Engine::init();
    registerHAnimPlugin();
    Engine::open(nil);
    Engine::start();
    StreamFile s;
    if(!s.open(argv[1],"rb")){fprintf(stderr,"open fail\n");return 1;}
    if(!findChunk(&s, ID_CLUMP, nil, nil)){fprintf(stderr,"no clump chunk\n");return 1;}
    Clump *c = Clump::streamRead(&s);
    if(!c){fprintf(stderr,"read fail\n");return 1;}
    Frame *root = c->getFrame();
    printf("clump %s root=%p\n", argv[1], (void*)root);
    // walk all frames depth-first, print local matrix and parent
    struct W { Frame *f; int depth; };
    W stack[1024]; int sp=0; stack[sp++]={root,0};
    while(sp){
        W w=stack[--sp];
        Frame *f=w.f;
        printf("FRAME depth=%d ptr=%p parent=%p\n", w.depth,(void*)f,(void*)f->getParent());
        printmat("  local", f->matrix);
        Matrix *ltm=f->getLTM();
        if(ltm) printmat("  ltm  ", *ltm);
        // push children reversed for order
        Frame *kids[256]; int n=0;
        for(Frame *k=f->child;k && n<256;k=k->next) kids[n++]=k;
        for(int i=n-1;i>=0;i--) if(sp<1024) stack[sp++]={kids[i],w.depth+1};
    }
    HAnimHierarchy *h = HAnimHierarchy::get(c);
    if(h){
        h->attach();
        printf("HANIM numNodes=%d flags=%x parentFrame=%p\n", h->numNodes, h->flags, (void*)h->parentFrame);
        for(int i=0;i<h->numNodes;i++){
            HAnimNodeInfo &ni=h->nodeInfo[i];
            printf("NODE i=%d id=%d index=%d flags=%d frame=%p\n", i,ni.id,ni.index,ni.flags,(void*)ni.frame);
            if(ni.frame){
                printmat("  local", ni.frame->matrix);
                Matrix *ltm=ni.frame->getLTM();
                if(ltm) printmat("  ltm  ", *ltm);
            }
        }
    } else printf("no hanim\n");
    return 0;
}
