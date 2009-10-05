#ifndef CONFIG_H
#define CONFIG_H

struct config_operations {
	void (*init) (char *, char **, int *);
	int (*target_add) (u32 *, char *);
	int (*target_stop) (u32);
	int (*target_del) (u32);
	int (*lunit_add) (u32, u32, char *);
	int (*lunit_stop) (u32, u32);
	int (*lunit_del) (u32, u32);
	int (*param_set) (u32, u64, int, u32, struct iscsi_param *);
	int (*account_add) (u32, int, char *, char *);
	int (*account_del) (u32, int, char *);
	int (*account_query) (u32, int, char *, char *);
	int (*account_list) (u32, int, u32 *, u32 *, char *, size_t);
	int (*initiator_allow) (u32, int, char *);
	int (*target_allow) (u32, struct sockaddr *);
};

extern struct config_operations *cops;

#endif
