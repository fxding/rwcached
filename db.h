#ifndef __DB_H_
#define __DB_H_


int db_save(char *filename);

int db_load(char *filename);

int db_bgsave(char *filename);

void db_bgsaveDoneHandler(int childstat);



#endif // __DB_H_
