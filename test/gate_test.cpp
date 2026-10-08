// gate_test.cpp - proximity gate, 34 assertions.
//
// Links against the REAL src/proximity.cpp. It used to slice the gate out of
// the .ino with string markers, because a sketch has no header to include;
// src/pure.h removed that need, and with it a whole class of test-drift bug.
//
//   g++ -std=gnu++17 -I ../Vulpecula/src gate_test.cpp \
//       ../Vulpecula/src/proximity.cpp -o /tmp/gate_test && /tmp/gate_test
//
// No Arduino headers, no stubs, no board. That is the point of pure.h.
#include "pure.h"
#include <initializer_list>

static int fails=0;
static void chk(bool c,const char*w){printf("  [%s] %s\n",c?"PASS":"FAIL",w); if(!c)fails++;}
static double fspl(double d,double f){return 20*log10(d)+20*log10(f)-147.55;}
static tier_t feed(rband_t b,int8_t r,int n,tier_t prev,const ranging_ref_t*ref=nullptr){
    rssi_ring_t g; ring_reset(&g);
    for(int i=0;i<n;i++) ring_push(&g,r,1000u+(uint32_t)i*50u);
    return prox_resolve_tier(&g,b,ref,prev,1000u+(uint32_t)n*50u);
}
int main(){
    prox_cal_reset_defaults();
    const double F24=2437e6;
    printf("\n== link budget, 2.4GHz, +17dBm camera ==\n");
    struct{const char*n;double d,w;bool e;}cs[]={
        {"in room 2m",2,0,true},{"in room 4m",4,0,true},
        {"next room 2m +12dB",2,12,false},{"next room 5m +12dB",5,12,false},
        {"corridor 8m +6dB",8,6,false},{"two rooms 6m +24dB",6,24,false}};
    for(auto&c:cs){int r=(int)lround(17.0-fspl(c.d,F24)-c.w);
        tier_t t=feed(BAND_24,(int8_t)r,5,TIER_AMBIENT); char b[96];
        snprintf(b,sizeof(b),"%-20s %4ddBm -> %-8s",c.n,r,tier_name(t));
        chk((t>=TIER_NEAR)==c.e,b);}
    printf("\n== CONTACT vs through-wall adversary ==\n");
    for(double tx:{17.0,20.0}) for(double w:{10.0,12.0,15.0}){
        int r=(int)lround(tx-fspl(0.3,F24)-w);
        tier_t t=feed(BAND_24,(int8_t)r,5,TIER_AMBIENT); char b[110];
        snprintf(b,sizeof(b),"%+.0fdBm 0.3m behind %.0fdB wall: %ddBm -> %s",tx,w,r,tier_name(t));
        chk(t<TIER_CONTACT,b);}
    {int r=(int)lround(23.0-fspl(0.3,F24)-8.0);
     tier_t t=feed(BAND_24,(int8_t)r,5,TIER_AMBIENT); char b[110];
     snprintf(b,sizeof(b),"KNOWN GAP +23dBm thru 8dB drywall %ddBm -> %s",r,tier_name(t));
     chk(t==TIER_CONTACT,b);}
    {int r=(int)lround(17.0-fspl(0.25,F24));
     chk(feed(BAND_24,(int8_t)r,5,TIER_AMBIENT)==TIER_CONTACT,"in-room 0.25m -> CONTACT");}
    printf("\n== min-packet rule and hysteresis ==\n");
    chk(feed(BAND_24,-25,1,TIER_AMBIENT)==TIER_AMBIENT,"1 packet does not trip");
    chk(feed(BAND_24,-25,2,TIER_AMBIENT)==TIER_AMBIENT,"2 packets do not trip");
    chk(feed(BAND_24,-25,3,TIER_AMBIENT)>=TIER_NEAR,   "3 packets do trip");
    int8_t marg=(int8_t)(g_cal.near_thresh[BAND_24]-2);
    chk(feed(BAND_24,marg,5,TIER_AMBIENT)==TIER_AMBIENT,"2dB below: no entry");
    chk(feed(BAND_24,marg,5,TIER_NEAR)==TIER_NEAR,      "2dB below: holds (hysteresis)");
    chk(feed(BAND_24,(int8_t)(g_cal.near_thresh[BAND_24]-8),5,TIER_NEAR)==TIER_AMBIENT,
        "8dB below: drops out");
    printf("\n== BLE reference removes TX-power error (24dB span) ==\n");
    {const double FB=2440e6; int rc=(int)lround(-20.0-fspl(2.0,FB));
     chk(feed(BAND_BLE,(int8_t)rc,5,TIER_AMBIENT)==TIER_AMBIENT,
         "raw gating MISSES a -20dBm advertiser at 2m");}
    for(int8_t tx:{(int8_t)4,(int8_t)-20,(int8_t)0,(int8_t)-8}){
        ranging_ref_t rf=prox_ref_from_txpower(tx); char b[110];
        int8_t a2=(int8_t)lround(rf.rssi_at_1m-10.0*g_cal.pathloss_n*log10(2.0));
        snprintf(b,sizeof(b),"TX %+3ddBm at 2.0m (%ddBm) -> NEAR",tx,a2);
        chk(feed(BAND_BLE,a2,5,TIER_AMBIENT,&rf)==TIER_NEAR,b);
        int8_t a5=(int8_t)lround(rf.rssi_at_1m-10.0*g_cal.pathloss_n*log10(5.0));
        snprintf(b,sizeof(b),"TX %+3ddBm at 5.0m (%ddBm) -> rejected",tx,a5);
        chk(feed(BAND_BLE,a5,5,TIER_AMBIENT,&rf)==TIER_AMBIENT,b);}
    {ranging_ref_t rf=prox_ref_from_ibeacon(-59);
     chk(rf.rssi_at_1m==-59,"iBeacon measured power used directly as 1m ref");
     int8_t a2=(int8_t)lround(-59-10.0*g_cal.pathloss_n*log10(2.0));
     chk(feed(BAND_BLE,a2,5,TIER_AMBIENT,&rf)==TIER_NEAR,"iBeacon at 2m -> NEAR");}
    {ranging_ref_t rf=prox_ref_from_eddystone(-18);
     chk(rf.rssi_at_1m==-59,"Eddystone 0m power -41dB -> 1m ref");}
    printf("\n== peak vs median under multipath nulls ==\n");
    {rssi_ring_t g; ring_reset(&g);
     for(int i=0;i<20;i++) ring_push(&g,(i%3==0)?(int8_t)-28:(int8_t)-72,1000u+(uint32_t)i*50u);
     uint32_t now=1000u+20*50u;
     tier_t t=prox_resolve_tier(&g,BAND_24,nullptr,TIER_AMBIENT,now);
     int8_t med=ring_median(&g,now,MEDIAN_WINDOW_MS), pk=ring_peak(&g,now,PEAK_WINDOW_MS);
     char b[110]; snprintf(b,sizeof(b),"peak %ddBm median %ddBm -> %s",pk,med,tier_name(t));
     chk(t>=TIER_NEAR,b);
     chk(med<g_cal.near_thresh[BAND_24],"median alone would have rejected it");}
    {rssi_ring_t g; ring_reset(&g);
     for(int i=0;i<12;i++) ring_push(&g,(i==2||i==7)?(int8_t)-28:(int8_t)-72,1000u+(uint32_t)i*50u);
     chk(prox_resolve_tier(&g,BAND_24,nullptr,TIER_AMBIENT,1600u)==TIER_AMBIENT,
         "2 strong samples do not alert - hold still longer");}
    printf("\n== distance estimate ==\n");
    {ranging_ref_t rf=prox_ref_from_ibeacon(-59); rssi_ring_t g; ring_reset(&g);
     int8_t a=(int8_t)lround(-59-10.0*g_cal.pathloss_n*log10(1.5));
     for(int i=0;i<9;i++) ring_push(&g,a,1000u+(uint32_t)i*50u);
     float d=prox_distance_m(&g,&rf,1500u); char b[80];
     snprintf(b,sizeof(b),"1.50m target estimated at %.2fm",d);
     chk(fabsf(d-1.5f)<0.2f,b);}
    printf("\n%s (%d failures)\n\n",fails?"TESTS FAILED":"ALL TESTS PASSED",fails);
    return fails?1:0;
}
