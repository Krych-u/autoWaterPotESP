Add or edit the following line in your postgresql.conf :

listen_addresses = '*'
Add the following line as the first line of pg_hba.conf. 
It allows access to all databases for all users with an encrypted password:

# TYPE DATABASE USER CIDR-ADDRESS  METHOD
host  all  all 0.0.0.0/0 scram-sha-256