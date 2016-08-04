import logging
import threading
import yaml

logger = logging.getLogger(__name__)


class Mapper(object):
    """Mapping of str<-->id bijection.

    Mapper is not thread-safe. Its purpose is to serve ``UnifiedMapper``, which
    is thread-safe. It is advisable to access Mapper information through
    UnifiedMapper.
    """
    def __init__(self, pair_iterable=None):
        """Constructor.

        Args:
            pair_iterable: an iterable object which each iteration gives a pair
                of (num, str) representing num:str mapping. An example of this
                argument is ``[(1:"one"), (2:"two")]``.
        """
        self._str_id = {}
        self._id_str = {}
        self._id_obj = {}
        if pair_iterable:
            for (_id, _str) in pair_iterable:
                self.add(_id, _str)

    def __iter__(self):
        for i in self._id_str:
            yield (i, self._id_str[i])

    def get_id(self, _str):
        """Returns the ID of the given string, or ``None`` if not found."""
        try:
            return self._str_id[_str]
        except KeyError:
            return None

    def get_str(self, _id):
        """Returns the string of the given ID, or ``None`` if not found."""
        try:
            return self._id_str[_id]
        except KeyError:
            return None

    def set_obj(self, _id, obj):
        self._id_obj[_id] = obj

    def get_obj(self, _id):
        try:
            return self._id_obj[_id]
        except KeyError:
            return None

    def add(self, _id, _str):
        """Add _id:_str into the mapping.

        Args:
            _id(num): the numeric identifier.
            _str(str): the string.

        Raises:
            KeyError: if _id or _str causes a conflict in the Mapper (e.g.
                existing _id, but maps to different _str).
        """
        if _id in self._id_str:
            if self._id_str[_id] != _str:
                raise KeyError("id_str[%d]: '%s' != str: '%s'" % (
                                    _id, self._id_str[_id], _str
                                ))
            return
        if _str in self._str_id:
            if self._str_id[_str] != _id:
                raise KeyError("str_id['%s']: %d != id: %d" % (
                                    _str, self._str_id[_str], _id
                            ))
            return
        self._id_str[_id] = _str
        self._str_id[_str] = _id

    def batch_add(self, pair_iterable):
        """Insert a batch of id:str mappings.

        Args:
            pair_iterable: an iterable object that gives a pair of (num, str)
                representing id:str for each iteration.
        """
        for (_id, _str) in pair_iterable:
            self.add(_id, _str)

    def __iadd__(self, other):
        """``+=`` operator override: adding contents of ``other`` into ``self``.

        The _id:_str mapping pair in ``other`` that does not exist in ``self``
        will be added into ``self``. The conflicting mappings will raise a
        KeyError.

        Args:
            other(pair_iterable)

        Raises:
            KeyError: if a mapping in ``other`` conflict with a mapping in
            ``self``.
        """
        self.batch_add(iter(other))
        return self


class UnifiedMapperIterator(object):
    def __init__(self, um):
        self._gn = um._gn
        self._um = um
        self._umapper_end = False
        self._umapper_iter = iter(um._umapper)
        self._unassigned_iter = iter(um._unassigned)

    def __iter__(self):
        return self

    def next(self):
        if self._gn != self._um._gn:
            # This is useful for debugging races
            raise RuntimeError("UnifiedMapper changes during iteration.")
        if (self._umapper_end):
            return (None, next(self._unassigned_iter))
        try:
            return next(self._umapper_iter)
        except StopIteration:
            self._umapper_end = True
        return (None, next(self._unassigned_iter))

class UnifiedMapper(object):
    """UnifiedMapper

    UnifiedMapper is a collection of ``Mapper``s. It has routines to aggregate
    strings from all mappers and unify them into an internal ``unassigned`` set,
    accessible via ``.get_unassigned()``.  The caller can then assign the
    unified ID thrugh ``.assign(ID, STR)`` interface, or let UnifiedMapper
    automatically assign the IDs by ``.auto_assign()`` interface.

    The ``unassigned`` set is automatically updated when the underlying Mapper
    changed. So, the ``.get_unassigned()`` returns a copy of the unassigned set
    instead of the reference to the set itself, so that the caller's iterator
    won't break when the set changed.

    UnifiedMapper data modifying/accessing methods are thread-safe. Iterable
    methods (such as .items()) are NOT thread-safe.

    Example:
        >>> from abhttp import Mapper, UnifiedMapper
        >>> map_one = Mapper([(1, "One."), (2, "Two.")])
        >>> map_two = Mapper([(1, "Two."), (2, "Three.")])
        >>> um = UnifiedMapper([("one", map_one), ("two", map_two)])
        >>> um.translate_id("one", "two", 2)
        1
        >>> um.get_unassigned()
        set(['Two.', 'Three.', 'One.'])
        >>> um.auto_assign()
        >>> [x for x in um]
        [(1, 'Two.'), (2, 'Three.'), (3, 'One.')]
        >>> um.assign(100, "One Hundred.")
        >>> [x for x in um]
        [(1, 'Two.'), (2, 'Three.'), (3, 'One.'), (100, 'One Hundred.')]
        >>> um.get_ustr(100)
        'One Hundred.'
        >>> um.get_uid("One Hundred.")
        100
        >>> um.translate_id("one", um.UNIFIED, 1)
        3
        >>> map_one.add(99, "Ninety Nine.")
        >>> map_one.add(98, "Ninety Eight.")
        >>> [x for x in um]
        [(1, 'Two.'), (2, 'Three.'), (3, 'One.'), (100, 'One Hundred.'), (None,
        'Ninety Eight.'), (None, 'Ninety Nine.')]
        >>> um.get_unassigned()
        set(['Ninety Eight.', 'Ninety Nine.'])


    Private Attributes:
        _mappers([Mapper]): list of mappers
        _umapper(Mapper): the Mapper to hold unified mapper info
        _unassigned(set(str)): set of strings that are not yet have a unified ID
            (in ``_umapper``).
    """

    UNIFIED = ""

    def __init__(self, named_mappers=None):
        """Constructor.

        Args:
            named_mapper([(str, Mapper)]): a list of str-Mapper tuple. The
                string is the name of the Mapper, which will be used to identify
                it.
        """
        self._gn = 0
        self._mappers = {}
        self._umapper = Mapper()
        self._unassigned = set()
        self._lock = threading.Lock()
        if named_mappers:
            for (_name, _mapper) in named_mappers:
                self.add_mapper(_name, _mapper)
        self._mappers[self.UNIFIED] = self._umapper

    def __del__(self):
        pass

    def __iter__(self):
        return UnifiedMapperIterator(self)

    def _check_unassigned(self, _str):
        """Check if ``_str`` is assigned; if not, add to ``_unassigned`` set."""
        # A self._lock must be held
        if _str not in self._umapper._str_id:
            self._gn += 1
            self._unassigned.add(_str)

    def add_mapper(self, name, m):
        """Add a Mapper into the UnifiedMapper.

        IMPORTANT: After the mapper ``m`` has been added, the UnifiedMapper owns
        the mapper ``m``. The mapper ``m`` should not be modified directly.
        """
        self._lock.acquire()
        try:
            ms = self._mappers
            if name in ms:
                raise KeyError("'%s' existed" % (name))
            ms[name] = m
            for _str in m._str_id:
                self._check_unassigned(_str)
        finally:
            self._lock.release()

    def remove_mapper(self, name):
        """Remove a mapper from the UnifiedMapper."""
        self._lock.acquire()
        try:
            ms = self._mappers
            del ms[name]
            # Update the ``unassigned`` after remove.
            self._unassigned.clear()
            for m in ms.itervalues():
                self._unassigned.update(x for x in m._str_id
                                                if x not in self._umapper._str_id)
        finally:
            self._lock.release()

    def update_mapper(self, name, itr):
        """Update mapper ``name`` inside UnifiedMapper.

        Args:
            itr([(_id,_str,_obj)]): id-str-obj tuple iterable.
        """
        self._lock.acquire()
        try:
            m = self._mappers[name]
            for (_id, _str, _obj) in itr:
                m.add(_id, _str)
                m.set_obj(_id, _obj)
                self._check_unassigned(_str)
        finally:
            self._lock.release()

    def get_mapper(self, name):
        """Getter function for a Mapper in the UnifiedMapper."""
        return self._mappers[name]

    def mapper_iter(self):
        for (k, m) in self._mappers.items():
            if k == self.UNIFIED:
                continue
            yield m

    def get_id(self, mapper_name, _str):
        """Get an ID of a string in a specific Mapper."""
        self._lock.acquire()
        try:
            return self._mappers[mapper_name][_str]
        finally:
            self._lock.release()

    def get_str(self, mapper_name, _id):
        """Get a string of an ID in a specific Mapper."""
        self._lock.acquire()
        try:
            return self._mappers[mapper_name][_id]
        finally:
            self._lock.release()

    def get_uid(self, _str):
        """Get a unified ID of a string."""
        self._lock.acquire()
        try:
            return self._umapper.get_id(_str)
        finally:
            self._lock.release()

    def get_ustr(self, _uid):
        """Get a unified string of an ID."""
        self._lock.acquire()
        try:
            return self._umapper.get_str(_uid)
        finally:
            self._lock.release()

    def translate_id(self, mapper_name0, mapper_name1, _id0):
        """Translate IDs among mappers.

        Translate ``_id0`` in ``mapper_name0`` to the ID in ``mapper_name1``
        space. If such ID does not exist, ``None`` is returned.

        UnifiedMapper.UNIFIED constant can be used to refer to the unified map.

        Args:
            mapper_name0(str): the name of the FROM mapper.
            mapper_name1(str): the name of the TO mapper.
            _id0(int): ID in mapper_name0 space.

        Returns:
            int: the ID in mapper_name1 space.
            None: if ``_id0`` does not exist in mapper_name0 space, or there is
                no equivalent ID of ``_id0`` in mapper_name1 space.
        """
        self._lock.acquire()
        try:
            m0 = self._mappers[mapper_name0]
            m1 = self._mappers[mapper_name1]
            _str = m0.get_str(_id0)
            if not _str:
                return None
            return m1.get_id(_str)
        finally:
            self._lock.release()

    def get_unassigned(self):
        """Obtain a copy of unassigned set."""
        self._lock.acquire()
        try:
            return self._unassigned.copy()
        finally:
            self._lock.release()

    def get_max_id(self):
        return max(self._umapper._id_str) + 1

    def auto_assign(self):
        """Automatically assign the unassigned."""
        self._lock.acquire()
        try:
            if len(self._umapper._id_str):
                _next_id = max(self._umapper._id_str) + 1
            else:
                _next_id = 0
            ulist = [x for x in self._unassigned]
            ulist.sort(reverse=True)
            while ulist:
                _str = ulist.pop()
                self._unassigned.remove(_str)
                self._umapper.add(_next_id, _str)
                _next_id += 1
        finally:
                self._lock.release()

    def _assign(self, uid, ustr):
        """Assign, must be called with self._lock acquired."""
        self._umapper.add(uid, ustr)
        try:
            self._unassigned.remove(ustr)
        except KeyError:
            pass # it's OK to try to remove the _str that is not there yet.

    def assign(self, uid, ustr):
        """Assign ``_id``<-->``_str`` mapping."""
        self._lock.acquire()
        try:
            self._assign(uid, ustr)
        finally:
            self._lock.release()

    def get_obj(self, uid):
        """Access the contextual object associated to the ``uid``."""
        self._lock.acquire()
        try:
            return self._umapper.get_obj(uid)
        finally:
            self._lock.release()

    def set_obj(self, uid, obj):
        """Set the contextual object associated to the ``uid``."""
        self._lock.acquire()
        try:
            self._umapper.set_obj(uid, obj)
        finally:
            self._lock.release()

    def items(self):
        for (_id, _str) in iter(self):
            yield (_id, _str, self._umapper.get_obj(_id))

    def save(self, fpath):
        """Save the unified mapper into the ``fpath``."""
        self._lock.acquire()
        try:
            f = open(fpath, "w")
            # save as a list of 3-tuple of (id,str,obj)
            yaml.dump([x for x in self.items()], stream=f)
            f.close()
        finally:
            self._lock.release()

    def load(self, fpath):
        """Load the unified mapper from ``fpath``."""
        f = open(fpath, "r")
        self._lock.acquire()
        try:
            y = yaml.load(f)
            for (_id, _str, _obj) in y:
                if _id == None:
                    self._unassigned.add(_str)
                    continue
                self._assign(_id, _str)
                self._umapper.set_obj(_id, _obj)
        finally:
            self._lock.release()
            f.close()
