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

Data returned from the HTTP requests is in [JSON syntax](http://www.json.org/).

# Dependencies

* GNU build tools: gcc, make, ld, etc
* [GNU microhttpd] (https://www.gnu.org/software/libmicrohttpd/): a C based library for HTTP


