
#ifndef _LINEAR_H
#define _LINEAR_H

struct linear_hash
{
  struct real_dev *dev0, *dev1;
};

struct linear_data
{
  struct linear_hash *hash_table; /* Dynamically allocated */
  struct real_dev *smallest;
  int nr_zones;
};

#endif

#ifndef _LINEAR_H
#define _LINEAR_H

struct linear_hash
{
  struct real_dev *dev0, *dev1;
};

struct linear_data
{
  struct linear_hash *hash_table; /* Dynamically allocated */
  struct real_dev *smallest;
  int nr_zones;
};

#endif
