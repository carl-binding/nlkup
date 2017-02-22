# Phone Number Portability

A common feature of telco operators is to support phone number portability.
Essentially this maps one phone number to some alias number. There is
a [Wikipedia article]  (https://en.wikipedia.org/wiki/Local_number_portability) which describes the topic to some detail.

The project is implementing an in-core data structure which supports lookup of phone numbers and retrieving their alias. We aim to
support up to 100 millions of phone numbers.

We make the functionality available via an HTTP based protocol to support
* lookup: given a number, retrieve its alias if any.
* insert: enter a new alias for a given number, possibly overwriting a previous entry
* delete: delete a number's alias
* dump to file: binary or textual dump of the entire lookup structure
* restore from file: restore data structure from binary dump.
* retrieve block: retrieve all aliases for a number block, where a number block is started with a 6 digit prefix.
* upload batch command file: to add/delete a number of phone-numbers and their aliases

Service performs checkpoints at regular interval. Old checkpoints are deleted.

Data returned from the HTTP requests is in [JSON syntax](http://www.json.org/).

## Why C?
Because it let's us pack our data. Java *does not allow* to inline arrays into objects; each array - even when of fixed length - needs a reference (pointer). 
Nor does JavaScript pack data tightly into objects. 

node.js would have been an alternative to build up a HTTP service. But:
* it's not multi-threaded using shared memory. Why waste all these CPUs?
* JavaScript is not that great for packed data.
* Interfacing C/C++ from JavaScript/node.js seems requires serializing/deserializing data
* Untyped languages have sucked thirty years ago and still suck today (and Typescript is still too much bleeding edge).

There is enough support for most of the needed HTTP functionality in the GNU *microhttpd* library. The main pain is heap management in the C world. Although other things would also be
nice to have:
* JSON support: don't need much, there probably are open source libs available
* reg-exp support: idem, would be useful for HTTP header handling

# Dependencies

* GNU build tools: gcc, make, ld, C library, etc
* [GNU microhttpd] (https://www.gnu.org/software/libmicrohttpd/): a C based library for HTTP

# Todos/Options

* GUI work: we have a partial GUI using [datatables/editor](https://editor.datatables.net/) which could be extended and/or embellished.
* DB hook-up: user login, logging, back-up storage could be provided via DB access. Not yet implemented.
* two-level locking: when querying for ranges, would need to lock multiple blocks. A global lock should protect the block-lock acquisition and release.
* batch file processing not fully implemented yet.

