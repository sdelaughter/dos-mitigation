# Analysis Tools

## Overview

This folder contains tools for storing, parsing, and plotting experimental data generated by other tools in the repository.  Data is pre-parsed and stored in a PostgreSQL database, then pulled into pandas dataframes as needed for plotting, which is done using matplotlib and seaborn.

After following the setup procedure below, this process can be orchestrated using Jupyter notebooks:
- Use [import.ipynb](import.ipynb) to parse a session of data and add it to the database.
- Use [plot.ipynb](plot.ipynb) as a starting point for learning how to generate plots from the database.

If you'd like to take a more manual approach, those notebooks rely on helper functions defined in [db_parse.py](db_parse.py) and [db_helpers.py](db_helpers.py), to parse experiment outputs and to interface with the databse.

For very quick analysis and debugging purposes, you can use [csv_to_tps.py](csv_to_tps.py) to compute Transactions per Second from a single CSV file that has the format: `status, start_time, end_time`, which is what the provided [client scripts](../common/clients) will generate.

## Setup

*tested on Ubuntu 22.04*

1. Run `sudo db_setup.sh` to install dependencies, start a Postgres database service, and delete the postgres user's password to simplify interaction with the database (if you're concerned with data security you will probably want to modify this setup procedure).

2. Run `sudo -u postgres createuser --interactive` and enter your username when prompted.  This will allow you to interact with the databse directly instead of switching to the postgres user account.

3. `cd` into this analysis directory and run `python3 -m venv env` to create a python virtual environment in a new `env` directory (or simply `python -m venv env` if Python 3 is your system defualt).

4. Run `source env/bin/activate` to activate the new environment.

5. Run `pip3 install seaborn psycopg2 jupyter` to install necessary python libraries.

6. Move/copy your experiment data into the `data` directory (or specify an alternate path in the Jupyter notebooks).  If storing large amounts of data on an external volume, you can replace the data directory with a symlink to avoid copying/moving.  Within the `data` directory, create a folder named for your revision/materialization, and place your session directory inside that.  For example, the path should look like: `/usr/local/dos-mitigation/data/materialization_name/session_name/`.  The revision/materialization and session directory names will be used as nicknames in the database (this assumes a 1:1 correspondence between a revision and a materialization).  Within the session directory should be a hidden copy of the session's parameters (`.parameters.json`), as well as some number of timestamped directories, one per experiment.  Within each of those should be one directory per host, and so on following the structure that [run.py](../run.py) creates.  Note that results are typically only collected at client nodes, so while placeholder directories are created for all hosts the rest will likely be empty.

7. Import data with `import.ipynb` and analyze it with `plot.ipynb`.  Note that when using GUI elements to select materializations, sessions, etc. you should NOT re-run the cell after making your selection (doing so will revert the selection to its default value).  Simply click your selection (or command-click to select multiple options where applicable), and the value(s) you choose will be read by other cells later in the notebook.<br>In `plot.ipynb` you'll need to re-run the "Query Database" cell after changing your session/revision/materialization selections, and re-run the "Update and Apply Selections" cell after changing your "Map Parameters to Plot Features" or "Filter Data" selections.

## Database Schema

The database schema is defined in [schema.sql](schema.sql) and illustrated in [schema.pdf](schema.pdf) (via [dbdiagram.io](https://dbdiagram.io)).  It is designed to bridge the Merge testbed workflow with the experimental methodology defined in my thesis, with reapeated sets of four experiments controlling for separate and combined effects of DoS attacks and mitigations across arbitrary variables.

The `revisions` and `materializations` tables contain information related to those aspects of the Merge testbed.  Currently this data must be input and updated manually (look for `revision_row` and `materialization_row` in [import.ipynb](import.ipynb)).  The assumption is that you'll typically run many sessions on the same topology (with a single revision and materialization).  These tables aren't currently used for much, but will be valuable if conducting experiments across different hardware.

Each `session` consists of multiple `experiments`, during which each `host` collects some `data`.  Key `results` are then extracted from the raw `data`.  To clarify, the `data` table essentially contains (metric, timestamp, value) tuples, while the `results` table aggregates those into higher-layer metrics like overhead and efficiency, across the full timespan of the experiment.  Raw data is preserved in case it's needed, but most plots should be easier and faster to generate from these higher-level results.

## Erasing Data
The `drop.ipynb` notebook can be used to erase data from the database.  It *should* be able to drop all data for a single session or a single materialization, but these sometimes cause errors.  A more reliable approach is to run the `drop_all()` cell to erase the entire database, and then re-import any sessions you want to preserve.  Note that you'll first need to set `safety=False` in the "Settings" cell, as an added precuation to avoid unintended data loss.
