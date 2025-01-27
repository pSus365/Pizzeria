#!/bin/bash

gcc manager.c pizzeria.c -o manager_app
gcc cashier.c pizzeria.c -o cashier_app
gcc client.c pizzeria.c -lpthread -o client_app
gcc fireman.c pizzeria.c -o fireman_app
