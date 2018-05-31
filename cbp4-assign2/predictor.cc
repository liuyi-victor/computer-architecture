#include "predictor.h"
#include <string.h>
#include <stdio.h>
#include <bitset>

//UINT32      unsigned int
//INT32       int
//UINT64      unsigned long long
//COUNTER     unsigned long long

#define counter_entries	4096
#define two_level_width	64
#define history_table_entries	512
#define num_pattern_tables	8
/////////////////////////////////////////////////////////////
// 2bitsat
/////////////////////////////////////////////////////////////
static UINT32 two_bitcounter[counter_entries];

void InitPredictor_2bitsat() 
{
	int i;
	for(i =0; i < counter_entries; i++)
		two_bitcounter[i] = 1;
}

bool GetPrediction_2bitsat(UINT32 PC) 
{
	UINT32 tag = PC & 0xfff;
	if(two_bitcounter[tag] > 1)
		return TAKEN;
	else
		return NOT_TAKEN;
}

void UpdatePredictor_2bitsat(UINT32 PC, bool resolveDir, bool predDir, UINT32 branchTarget) 
{
	UINT32 tag = PC & 0xfff;
	unsigned short value = two_bitcounter[tag];
	if(resolveDir == TAKEN)
	{
		if(value < 3)
			two_bitcounter[tag] = value + 1;
	}
	else
	{
		if(value > 0)
			two_bitcounter[tag] = value - 1;
	}
}

/////////////////////////////////////////////////////////////
// 2level
/////////////////////////////////////////////////////////////
static UINT32 historyreg[history_table_entries];			//branch history register that is 2^9 = 512 entries index by PC [11:3]
static UINT32 patterntable[num_pattern_tables][two_level_width];	//8 pattern history tables each with 2^6 entries since the predictor is tracking 6 history bits

void InitPredictor_2level() 
{
	int i,j;
	for(i =0; i < history_table_entries; i++)
		historyreg[i] = 0;
	for(i =0; i < num_pattern_tables; i++)
	{
		for(j = 0; j < two_level_width; j++)
			patterntable[i][j] = 1;
	}
}

bool GetPrediction_2level(UINT32 PC) 
{
	UINT32 historytag = PC & 0b111111111000;
	historytag = historytag >> 3;
	UINT32 patterntag = PC & 0b111;		//which PHT table

	if(patterntable[patterntag][historyreg[historytag]] > 1)
		return TAKEN;
	else
		return NOT_TAKEN;
	return TAKEN;
}

void UpdatePredictor_2level(UINT32 PC, bool resolveDir, bool predDir, UINT32 branchTarget) 
{
	UINT32 historytag = PC & 0b111111111000;
	historytag = historytag >> 3;
	UINT32 patterntag = PC & 0b111;
	
	//updates the branch history register
	UINT32 history = historyreg[historytag];
	historyreg[historytag] = ((history << 1) | resolveDir) & 0b111111;

	//updates the pattern history table
	UINT32 value = patterntable[patterntag][history];
	if(resolveDir == TAKEN)
	{
		if(value < 3)
			patterntable[patterntag][history] = value + 1;
	}
	else
	{
		if(value > 0)
			patterntable[patterntag][history] = value - 1;
	}
}

/////////////////////////////////////////////////////////////
// openend
/////////////////////////////////////////////////////////////

bitset<15> GHR;
int openendPHT[3][1 << 14];
int twobitOpen[32768];
int gshare_counter;
int twobit_counter;
int inst_counter;
int inst_counter_limit = 256000;
int gshare_PHT[1<<15];

template <int N1, int N2>
bitset <N1 + N2> bitset_concat(const bitset<N1> &b1, const bitset<N2> &b2) {
    string s1 = b1.to_string();
    string s2 = b2.to_string();
    return bitset<N1+N2> (s1 + s2);
}

template <int N>
bitset<N> bitset_hash(bitset<N> V) {
    bool last_bit = V[N-1];
    bool first_bit = V[0];
    V = V >> 1;
    V[N-1] = last_bit ^ first_bit;
    return V;
}

template <int N>
bitset<N> bitset_invhash(bitset<N> V) {
    bool n_xor_1_bit = V[N-1];
    bool n_bit = V[N-2];
    V = V << 1;
    V[0] = n_xor_1_bit ^ n_bit;
    return V;
}

template <int N>
bitset<N> get_f1(bitset<N> V1, bitset<N> V2) {
    return (bitset_hash<N>(V1) ^ bitset_invhash<N>(V2) ^ V2);
}
template <int N>
bitset<N> get_f2(bitset<N> V1, bitset<N> V2) {
    return (bitset_hash<N>(V1) ^ bitset_invhash<N>(V2) ^ V1);
}
template <int N>
bitset<N> get_f3(bitset<N> V1, bitset<N> V2) {
    return (bitset_invhash<N>(V1) ^ bitset_hash<N>(V2) ^ V2);
}

bitset<14> V1;
bitset<14> V2;
void InitPredictor_openend() {
    for (int i = 0; i < (1 << 15); i++)
        gshare_PHT[i] = 3;
}

bool GetPrediction_openend(UINT32 PC) {
    /*
    bitset<32> bitset_PC(PC);
    bitset<40> V = bitset_concat<32,8>(bitset_PC, GHR);
    bitset<14> V1(V.to_string().substr(26,14));
    bitset<14> V2(V.to_string().substr(12,14));
    UINT32 f1 = get_f1<14>(V1,V2).to_ulong();
    UINT32 f2 = get_f2<14>(V1,V2).to_ulong();
    UINT32 f3 = get_f3<14>(V1,V2).to_ulong();
    f1 = f1 & ((1 << 14) - 1);
    f2 = f2 & ((1 << 14) - 1);
    f3 = f3 & ((1 << 14) - 1);
    
    int not_taken_counter = 0;
    if (openendPHT[0][f1] < 2) not_taken_counter++;
    if (openendPHT[1][f2] < 2) not_taken_counter++;
    if (openendPHT[2][f3] < 2) not_taken_counter++;
    //printf("f1 %d f2 %d f3 %d count %d\n", f1, f2, f3, not_taken_counter); 
    if (not_taken_counter >= 2) return NOT_TAKEN;
    return TAKEN;*/
    UINT32 f = PC ^ GHR.to_ulong();
    f = f & ((1 << 15) - 1);
    //printf("f: %d\n", f);
    if (gshare_PHT[f] >= 4) return TAKEN;
    return NOT_TAKEN;
}

void UpdatePredictor_openend(UINT32 PC, bool resolveDir, bool predDir, UINT32 branchTarget) {
    /*
    bitset<32> bitset_PC(PC);
    bitset<40> V = bitset_concat<32,8>(bitset_PC, GHR);
    bitset<14> V1(V.to_string().substr(26,14));
    bitset<14> V2(V.to_string().substr(12,14));
    UINT32 f1 = get_f1<14>(V1,V2).to_ulong();
    UINT32 f2 = get_f2<14>(V1,V2).to_ulong();
    UINT32 f3 = get_f3<14>(V1,V2).to_ulong();
    f1 = f1 & ((1 << 14) - 1);
    f2 = f2 & ((1 << 14) - 1);
    f3 = f3 & ((1 << 14) - 1);
    
    // update history table
    GHR <<= 1;
    if (resolveDir == TAKEN) GHR |= 1;
    // update three banks
    if (resolveDir == predDir) {
        if (resolveDir == TAKEN) {
            if (openendPHT[0][f1] == 2) openendPHT[0][f1]++;
            if (openendPHT[1][f2] == 2) openendPHT[1][f2]++;
            if (openendPHT[2][f3] == 2) openendPHT[2][f3]++;
        }
        else {
            if (openendPHT[0][f1] == 1) openendPHT[0][f1]--;
            if (openendPHT[1][f2] == 1) openendPHT[1][f2]--;
            if (openendPHT[2][f3] == 1) openendPHT[2][f3]--;
        }
    } else {
        if (resolveDir == TAKEN) {
            if (openendPHT[0][f1] != 3) openendPHT[0][f1]++;
            if (openendPHT[1][f2] != 3) openendPHT[1][f2]++;
            if (openendPHT[2][f3] != 3) openendPHT[2][f3]++;
        } else {
            if (openendPHT[0][f1] != 0) openendPHT[0][f1]--;
            if (openendPHT[1][f2] != 0) openendPHT[1][f2]--;
            if (openendPHT[2][f3] != 0) openendPHT[2][f3]--;
        }
    }

 */
    UINT32 f = PC ^ GHR.to_ulong();
    f = f & ((1 << 15) - 1);
    if (resolveDir == TAKEN) {
        if (gshare_PHT[f] != 7) gshare_PHT[f]++;
    } else {
        if (gshare_PHT[f] != 0) gshare_PHT[f]--;
    }
    GHR >>= 1;
    if (resolveDir == TAKEN) GHR[14] = 1;
}

/*
void InitPredictor_openend() {

}

bool GetPrediction_openend(UINT32 PC) {

  return TAKEN;
}

void UpdatePredictor_openend(UINT32 PC, bool resolveDir, bool predDir, UINT32 branchTarget) {

}
*/
