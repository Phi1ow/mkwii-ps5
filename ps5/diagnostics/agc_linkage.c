/* Inspect AGC's CPU-side semantic matching before submitting any GPU work.
 * The original prepared headers are restored before the normal drawing test.
 * No modified mapping is ever submitted to the GPU. */
extern int sceAgcCreateInterpolantMapping(void*, void*, void*);

static void linkage_line(unsigned int test, unsigned int word, int rc,
                         const CxRegister* mapping) {
    char line[192]; int n=0;
    const char* prefix="[mkw-linkage] case word rc records ";
    for(int i=0;prefix[i];++i)line[n++]=prefix[i];
    n=hex_append(line,n,test,4);line[n++]=' ';
    n=hex_append(line,n,word,8);line[n++]=' ';
    n=hex_append(line,n,(unsigned int)rc,8);
    const unsigned int* words=(const unsigned int*)mapping;
    for(int i=0;i<8;++i){line[n++]=' ';n=hex_append(line,n,words[i],8);}
    line[n++]='\n';line[n]=0;mkw_diagnostic_log(line);
}

static int inspect_agc_linkage(void* vs,void* ps) {
    unsigned char* vh=(unsigned char*)vs;
    unsigned char* ph=(unsigned char*)ps;
    unsigned int* outputs=*(unsigned int**)(vh+56);
    unsigned int* inputs=*(unsigned int**)(ph+48);
    /* Stop if preparation does not confirm this candidate layout. */
    if((unsigned char*)outputs!=vh+0x90 || (unsigned char*)inputs!=ph+0x90 ||
       *(unsigned short*)(vh+0x56)!=2 || *(unsigned short*)(ph+0x50)!=2 ||
       outputs[0]!=0x0f || outputs[1]!=0x110 || inputs[0]!=0x0f || inputs[1]!=0x10) {
        mkw_diagnostic_log("[mkw-linkage] FAIL prepared semantic layout differs\n");
        return 1;
    }
    mkw_diagnostic_log("[mkw-linkage] prepared relative pointers and two-record counts confirmed\n");
    struct { unsigned long long before; CxRegister records[32]; unsigned long long after; } m;
    CxRegister baseline[32];
    int failed=0;
    for(unsigned int test=0;test<28;++test) {
        unsigned int word=0;
        outputs[1]=0x110;inputs[1]=0x10;
        if(test>=1 && test<=13){word=0x110^(1u<<(test-1));outputs[1]=word;}
        if(test>=14 && test<=26){word=0x10^(1u<<(test-14));inputs[1]=word;}
        m.before=m.after=0x9fa753126bd048ecULL;
        for(int i=0;i<32;++i){unsigned int* w=(unsigned int*)&m.records[i];w[0]=w[1]=0xcdcdcdcd;}
        int rc=sceAgcCreateInterpolantMapping(m.records,vs,ps);
        linkage_line(test,word,rc,m.records);
        if(m.before!=0x9fa753126bd048ecULL || m.after!=0x9fa753126bd048ecULL) {
            mkw_diagnostic_log("[mkw-linkage] FAIL mapping buffer guard\n");failed=1;break;
        }
        if(test==0){if(rc<0){failed=1;break;}for(int i=0;i<32;++i)baseline[i]=m.records[i];}
        if(test==27){
            if(rc<0)failed=1;
            const unsigned int* a=(const unsigned int*)baseline;
            const unsigned int* b=(const unsigned int*)m.records;
            for(int i=0;i<64;++i)if(a[i]!=b[i])failed=1;
        }
    }
    outputs[1]=0x110;inputs[1]=0x10;
    mkw_diagnostic_log(failed?"[mkw-linkage] FAIL restoration check\n":"[mkw-linkage] PASS 26 CPU mapping perturbations; baseline restored; no altered mapping submitted\n");
    return failed;
}
