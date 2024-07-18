#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bf.h"
#include "hp_file.h"
#include "record.h"
#define HP_ERROR -1
#define CALL_BF(call)       \
{                           \
  BF_ErrorCode code = call; \
  if (code != BF_OK) {         \
    BF_PrintError(code);    \
    return HP_ERROR;        \
  }                         \
}

int HP_CreateFile(char *fileName){
  BF_Block *hp_block;
  BF_Block_Init(&hp_block); //Allocating a temp block
  HP_info *info; 
  HP_block_info *block_info;

  int fileDesc;
  void* init_data;
  info = malloc(sizeof(HP_info));
  block_info = malloc(sizeof(HP_block_info));
  CALL_BF(BF_CreateFile(fileName)); 
  CALL_BF(BF_OpenFile(fileName, &fileDesc)); //Creating and opening the heap file in order to place the metadata we need

  CALL_BF(BF_AllocateBlock(fileDesc, hp_block)); //Allocating the first block for the heap file
  init_data = BF_Block_GetData(hp_block); //Getting its data
  info->block = hp_block;      // The first block of the heap file
  info->last_block_id = 0; // A counter to know how many blocks we have been initialized
  info->flag = "is Heap"; // A message to check if the file is a heap file
  info->records_block_size = (BF_BLOCK_SIZE-sizeof(HP_block_info)) / sizeof(Record); // >>
  block_info->record_block_size = 0; // block information
  memcpy(init_data, &info, sizeof(info));  // Using memcpy to change the block's data
  memcpy(init_data + sizeof(info), &block_info, sizeof(block_info)); // >>
  BF_Block_SetDirty(hp_block); //Using SetDirty since we changed the data
  CALL_BF(BF_UnpinBlock(hp_block)); //Taking care of the bytes allocated from BF_AllocateBlock()


  BF_Block_Destroy(&hp_block); //Taking care of the bytes allocated from BF_Block_Init()
  CALL_BF(BF_CloseFile(fileDesc));
  
  
  return 0;
}

HP_info* HP_OpenFile(char *fileName, int *file_desc){
  BF_Block *hp_block;
  HP_info *info;
  int fileDesc;
  void *data;
  CALL_BF(BF_OpenFile(fileName, &fileDesc)); //Opening the heap file
  BF_Block_Init(&hp_block); //Allocating a temp block
  CALL_BF(BF_GetBlock(fileDesc, 0, hp_block)); //Getting the first block
  file_desc = fileDesc;
  data = BF_Block_GetData(hp_block); //Getting its data
  memcpy(&info, data, sizeof(info)); //Using memcpy to get the information we need
  if(strcmp(info->flag, "is Heap") == 0){ // Make sure the first block has the our information
    printf("Heap FIle is fine!!!\n");
  }else{
    return NULL;
  }

  CALL_BF(BF_UnpinBlock(hp_block)); //Taking care of the bytes allocated from BF_GetBlock()
  BF_Block_Destroy(&hp_block); //Taking care of the bytes allocated from BF_Block_Init()

  return info;
}


int HP_CloseFile(int file_desc,HP_info* hp_info ){
  CALL_BF(BF_CloseFile(file_desc)); //Closing the heap file

  free(hp_info);

  return 0;
}

int HP_InsertEntry(int file_desc,HP_info* hp_info, Record record){
  
  static int slot = 0; //The slot in the block where the record will be placed
  static int block_counter = 1; //Counting each block, we start from 1 since block number zero is reserved for the special information
  int newblock = 0; //Flag to check if we allocated a new block or not
  void* data;
  BF_Block *hp_block;
  HP_block_info block_info;

  BF_Block_Init(&hp_block);//Allocating a temp block

  if(slot == 0){ //If slot is 0 we need to allocate a new block since the first entry is placed at slot number 0.
    CALL_BF(BF_AllocateBlock(file_desc, hp_block));
    newblock = 1;
  }else{ //Otherwise we get an already existing block to insert entries 1 from 7
    CALL_BF(BF_GetBlock(file_desc, block_counter , hp_block));
    newblock = 0;
  }
  data = BF_Block_GetData(hp_block);
  if (newblock){
    hp_info->last_block_id = block_counter; //Updating the last block id
    newblock = 0;
  }
  memcpy(&block_info, data+(hp_info->records_block_size*sizeof(Record)), sizeof(HP_block_info)); //Getting the block's information
  int offset = slot * sizeof(Record); //Offset is where the next entry will be placed, inside the block. 
  memcpy(data+offset, &record, sizeof(Record)); //Memcpy is used to change the block's data
  slot++;
  block_info.record_block_size = slot;
  
  memcpy(data+(hp_info->records_block_size*sizeof(Record)), &block_info, sizeof(block_info)); //We also need to change the block's information
  BF_Block_SetDirty(hp_block); //Using SetDirty since we changed the data 
  CALL_BF(BF_UnpinBlock(hp_block));//Taking care of the bytes allocated from BF_AllocateBlock() or BF_GetBlock()

   //We placed an entry at this slot, so we increment it for the next one
  if (block_info.record_block_size == hp_info->records_block_size){ // If we have reached the maximum number of entries in a block we need to allocate a new one
    slot = 0;
    block_counter++;
  }

  BF_Block_Destroy(&hp_block); //Taking care of the bytes allocated from BF_Block_Init()
  return 0;
}

int HP_GetAllEntries(int file_desc,HP_info* hp_info, int value){    
  int block_counter = 1;
  BF_Block *hp_block;
  HP_block_info block_info;
  int flag = 0; // the flag is used to check if we found any entries with the given value

  BF_Block_Init(&hp_block);

  while(block_counter<hp_info->last_block_id){ //While there are still blocks left to check
    void* data;
  
    CALL_BF(BF_GetBlock(file_desc, block_counter, hp_block));
    data = BF_Block_GetData(hp_block); // Getting the block's data
    memcpy(&block_info, data+(hp_info->records_block_size*sizeof(Record)), sizeof(HP_block_info)); //Getting the block's information
    for(int i = 0; i < block_info.record_block_size; i++){
      Record record;

      int offset = i * sizeof(Record); // Calculating how many slots i must move to take the record
      memcpy(&record, data+offset, sizeof(Record)); // coping the record to the temp variable
      if(record.id==value){ // if the record's id is the same as the given value we print it and braking the loop
        printf("%d %s %s %s \n", record.id, record.name, record.surname, record.city );
        flag = 1;
      }
    }
    

    CALL_BF(BF_UnpinBlock(hp_block));
    if (block_info.record_block_size == hp_info->records_block_size){ 
      block_counter++;
    }
  }


  BF_Block_Destroy(&hp_block);
  if (flag == 0){ // if we didn't found any entries with the given value return -1
      return -1;
  }
  return block_counter;
}

