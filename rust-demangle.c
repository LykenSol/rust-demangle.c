#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Public Rust Demangler API. */
typedef void (*rust_demangler_callback) (void *, const char *, size_t);
int rust_demangle_with_callback (const char *mangled, void *callback_opaque,
                                 rust_demangler_callback callback);
char *rust_demangle (const char *mangled);

/* Rust Demangler implementation. */

#define IS_DIGIT(c) ((c) >= '0' && (c) <= '9')
#define IS_UPPER(c) ((c) >= 'A' && (c) <= 'Z')
#define IS_LOWER(c) ((c) >= 'a' && (c) <= 'z')

struct rust_demangler
{
  const char *sym;
  size_t sym_len;

  void *callback_opaque;
  rust_demangler_callback callback;

  size_t next;
  int errored;
  int skipping_printing;
  int verbose;
  uint64_t bound_lifetime_depth;
};

#define ERROR_AND(x)                                                          \
  do                                                                          \
    {                                                                         \
      rdm->errored = 1;                                                       \
      x;                                                                      \
    }                                                                         \
  while (0);
#define CHECK_OR(cond, x)                                                     \
  if (!(cond))                                                                \
    ERROR_AND (x);

/* Parsing functions. */

static char
peek (const struct rust_demangler *rdm)
{
  if (rdm->next < rdm->sym_len)
    return rdm->sym[rdm->next];
  return 0;
}

static int
eat (struct rust_demangler *rdm, char c)
{
  if (peek (rdm) == c)
    {
      rdm->next++;
      return 1;
    }
  else
    return 0;
}

static char
next (struct rust_demangler *rdm)
{
  char c = peek (rdm);
  CHECK_OR (c, return 0);
  rdm->next++;
  return c;
}

static uint64_t
parse_integer_62 (struct rust_demangler *rdm)
{
  char c;
  uint64_t x;

  if (eat (rdm, '_'))
    return 0;

  x = 0;
  while (!eat (rdm, '_'))
    {
      c = next (rdm);
      x *= 62;
      if (IS_DIGIT (c))
        x += c - '0';
      else if (IS_LOWER (c))
        x += 10 + (c - 'a');
      else if (IS_UPPER (c))
        x += 10 + 26 + (c - 'A');
      else
        ERROR_AND (return 0);
    }
  return x + 1;
}

static uint64_t
parse_opt_integer_62 (struct rust_demangler *rdm, char tag)
{
  if (!eat (rdm, tag))
    return 0;
  return 1 + parse_integer_62 (rdm);
}

static uint64_t
parse_disambiguator (struct rust_demangler *rdm)
{
  return parse_opt_integer_62 (rdm, 's');
}

struct rust_mangled_ident
{
  /* ASCII part of the identifier. */
  const char *ascii;
  size_t ascii_len;

  /* Punycode insertion codes for Unicode codepoints, if any. */
  const char *punycode;
  size_t punycode_len;
};

static struct rust_mangled_ident
parse_ident (struct rust_demangler *rdm)
{
  char c;
  size_t start, len;
  int is_punycode;
  struct rust_mangled_ident ident;

  ident.ascii = NULL;
  ident.ascii_len = 0;
  ident.punycode = NULL;
  ident.punycode_len = 0;

  is_punycode = eat (rdm, 'u');

  c = next (rdm);
  CHECK_OR (IS_DIGIT (c), return ident);
  len = c - '0';

  if (c != '0')
    while (IS_DIGIT (peek (rdm)))
      len = len * 10 + (next (rdm) - '0');

  start = rdm->next;
  rdm->next += len;
  /* Check for overflows. */
  CHECK_OR ((start <= rdm->next) && (rdm->next <= rdm->sym_len), return ident);

  ident.ascii = rdm->sym + start;
  ident.ascii_len = len;

  if (is_punycode)
    {
      ident.punycode_len = 0;
      while (ident.ascii_len > 0)
        {
          ident.ascii_len--;

          /* The last '_' is a separator between ascii & punycode. */
          if (ident.ascii[ident.ascii_len] == '_')
            break;

          ident.punycode_len++;
        }
      CHECK_OR (ident.punycode_len > 0, return ident);
      ident.punycode = ident.ascii + (len - ident.punycode_len);
    }

  if (ident.ascii_len == 0)
    ident.ascii = NULL;

  return ident;
}

/* Printing functions. */

static void
print_str (struct rust_demangler *rdm, const char *data, size_t len)
{
  if (!rdm->errored && !rdm->skipping_printing)
    rdm->callback (rdm->callback_opaque, data, len);
}

#define PRINT(s) print_str (rdm, s, strlen (s))

static void
print_uint64 (struct rust_demangler *rdm, uint64_t x)
{
  char s[21];
  sprintf (s, "%" PRIu64, x);
  PRINT (s);
}

static void
print_uint64_hex (struct rust_demangler *rdm, uint64_t x)
{
  char s[17];
  sprintf (s, "%" PRIx64, x);
  PRINT (s);
}

static void
print_ident (struct rust_demangler *rdm, struct rust_mangled_ident ident)
{
  uint8_t *out, *p, d;
  size_t len, cap, punycode_pos, j;
  /* Punycode parameters and state. */
  uint32_t c;
  size_t base, t_min, t_max, skew, damp, bias, i;
  size_t delta, w, k, t;

  if (rdm->errored || rdm->skipping_printing)
    return;

  if (!ident.punycode)
    {
      print_str (rdm, ident.ascii, ident.ascii_len);
      return;
    }

  len = 0;
  cap = 4;
  while (cap < ident.ascii_len)
    {
      cap *= 2;
      /* Check for overflows. */
      CHECK_OR ((cap * 4) / 4 == cap, return );
    }

  /* Store the output codepoints as groups of 4 UTF-8 bytes. */
  out = (uint8_t *)malloc (cap * 4);
  CHECK_OR (out, return );

  /* Populate initial output from ASCII fragment. */
  for (len = 0; len < ident.ascii_len; len++)
    {
      p = out + 4 * len;
      p[0] = 0;
      p[1] = 0;
      p[2] = 0;
      p[3] = ident.ascii[len];
    }

  /* Punycode parameters and initial state. */
  base = 36;
  t_min = 1;
  t_max = 26;
  skew = 38;
  damp = 700;
  bias = 72;
  i = 0;
  c = 0x80;

  punycode_pos = 0;
  while (punycode_pos < ident.punycode_len)
    {
      /* Read one delta value. */
      delta = 0;
      w = 1;
      k = 0;
      do
        {
          k += base;
          t = k < bias ? 0 : (k - bias);
          if (t < t_min)
            t = t_min;
          if (t > t_max)
            t = t_max;

          CHECK_OR (punycode_pos < ident.punycode_len, goto cleanup);
          d = ident.punycode[punycode_pos++];

          if (IS_LOWER (d))
            d = d - 'a';
          else if (d >= 'A' && d <= 'J')
            d = 26 + (d - 'A');
          else
            ERROR_AND (goto cleanup);

          delta += d * w;
          w *= base - t;
        }
      while (d >= t);

      /* Compute the new insert position and character. */
      len++;
      i += delta;
      c += i / len;
      i %= len;

      /* Ensure enough space is available. */
      if (cap < len)
        {
          cap *= 2;
          /* Check for overflows. */
          CHECK_OR ((cap * 4) / 4 == cap, goto cleanup);
          CHECK_OR (cap >= len, goto cleanup);
        }
      p = (uint8_t *)realloc (out, cap * 4);
      CHECK_OR (p, goto cleanup);
      out = p;

      /* Move the characters after the insert position. */
      p = out + i * 4;
      memmove (p + 4, p, (len - i - 1) * 4);

      /* Insert the new character, as UTF-8 bytes. */
      p[0] = c >= 0x10000 ? 0xf0 | (c >> 18) : 0;
      p[1] = c >= 0x800 ? (c < 0x10000 ? 0xe0 : 0x80) | ((c >> 12) & 0x3f) : 0;
      p[2] = (c < 0x800 ? 0xc0 : 0x80) | ((c >> 6) & 0x3f);
      p[3] = 0x80 | (c & 0x3f);

      /* If there are no more deltas, decoding is complete. */
      if (punycode_pos == ident.punycode_len)
        break;

      i++;

      /* Perform bias adaptation. */
      delta /= damp;
      damp = 2;

      delta += delta / len;
      k = 0;
      while (delta > ((base - t_min) * t_max) / 2)
        {
          delta /= base - t_min;
          k += base;
        }
      bias = k + ((base - t_min + 1) * delta) / (delta + skew);
    }

  /* Remove all the 0 bytes to leave behind an UTF-8 string. */
  for (i = 0, j = 0; i < len * 4; i++)
    if (out[i] != 0)
      out[j++] = out[i];

  print_str (rdm, (const char *)out, j);

cleanup:
  free (out);
}

/* Print the lifetime according to the previously decoded index.
   An index of `0` always refers to `'_`, but starting with `1`,
   indices refer to late-bound lifetimes introduced by a binder. */
static void
print_lifetime_from_index (struct rust_demangler *rdm, uint64_t lt)
{
  char c;
  uint64_t depth;

  PRINT ("'");
  if (lt == 0)
    {
      PRINT ("_");
      return;
    }

  depth = rdm->bound_lifetime_depth - lt;
  /* Try to print lifetimes alphabetically first. */
  if (depth < 26)
    {
      c = 'a' + depth;
      print_str (rdm, &c, 1);
    }
  else
    {
      /* Use `'_123` after running out of letters. */
      PRINT ("_");
      print_uint64 (rdm, depth);
    }
}

/* Demangling functions. */

static void demangle_binder (struct rust_demangler *rdm);
static void demangle_path (struct rust_demangler *rdm, int in_value);
static void demangle_generic_arg (struct rust_demangler *rdm);
static void demangle_type (struct rust_demangler *rdm);
static int demangle_path_maybe_open_generics (struct rust_demangler *rdm);
static void demangle_dyn_trait (struct rust_demangler *rdm);
static void demangle_const (struct rust_demangler *rdm);
static void demangle_const_uint (struct rust_demangler *rdm);

/* Optionally enter a binder ('G') for late-bound lifetimes,
   printing e.g. `for<'a, 'b> `, and make those lifetimes visible
   to the caller (via depth level, which the caller should reset). */
static void
demangle_binder (struct rust_demangler *rdm)
{
  uint64_t i, bound_lifetimes;

  CHECK_OR (!rdm->errored, return );

  bound_lifetimes = parse_opt_integer_62 (rdm, 'G');
  if (bound_lifetimes > 0)
    {
      PRINT ("for<");
      for (i = 0; i < bound_lifetimes; i++)
        {
          if (i > 0)
            PRINT (", ");
          rdm->bound_lifetime_depth++;
          print_lifetime_from_index (rdm, 1);
        }
      PRINT ("> ");
    }
}

static void
demangle_path (struct rust_demangler *rdm, int in_value)
{
  char tag, ns;
  int was_skipping_printing;
  size_t i, backref, old_next;
  uint64_t dis;
  struct rust_mangled_ident name;

  CHECK_OR (!rdm->errored, return );

  switch (tag = next (rdm))
    {
    case 'C':
      dis = parse_disambiguator (rdm);
      name = parse_ident (rdm);

      if (!name.punycode)
        {
          /* Unescape `_[0-9_]`. */
          if (name.ascii_len > 1 && name.ascii[0] == '_')
            {
              name.ascii++;
              name.ascii_len--;
            }
        }
      print_ident (rdm, name);
      if (rdm->verbose)
        {
          PRINT ("[");
          print_uint64_hex (rdm, dis);
          PRINT ("]");
        }
      break;
    case 'N':
      ns = next (rdm);
      CHECK_OR (IS_LOWER (ns) || IS_UPPER (ns), return );

      demangle_path (rdm, in_value);
      PRINT ("::");

      dis = parse_disambiguator (rdm);
      name = parse_ident (rdm);

      if (IS_UPPER (ns))
        {
          /* Special namespaces, like closures and shims. */
          PRINT ("{");
          switch (ns)
            {
            case 'C':
              PRINT ("closure");
              break;
            case 'S':
              PRINT ("shim");
              break;
            default:
              print_str (rdm, &ns, 1);
            }
          if (name.ascii || name.punycode)
            {
              PRINT (":");
              print_ident (rdm, name);
            }
          PRINT ("#");
          print_uint64 (rdm, dis);
          PRINT ("}");
        }
      else
        /* Implementation-specific/unspecified namespaces. */
        print_ident (rdm, name);
      break;
    case 'M':
    case 'X':
      /* Ignore the `impl`'s own path.*/
      parse_disambiguator (rdm);
      was_skipping_printing = rdm->skipping_printing;
      rdm->skipping_printing = 1;
      demangle_path (rdm, in_value);
      rdm->skipping_printing = was_skipping_printing;
    case 'Y':
      PRINT ("<");
      demangle_type (rdm);
      if (tag != 'M')
        {
          PRINT (" as ");
          demangle_path (rdm, 0);
        }
      PRINT (">");
      break;
    case 'I':
      demangle_path (rdm, in_value);
      if (in_value)
        PRINT ("::");
      PRINT ("<");
      for (i = 0; !rdm->errored && !eat (rdm, 'E'); i++)
        {
          if (i > 0)
            PRINT (", ");
          demangle_generic_arg (rdm);
        }
      PRINT (">");
      break;
    case 'B':
      backref = parse_integer_62 (rdm);
      if (!rdm->skipping_printing)
        {
          old_next = rdm->next;
          rdm->next = backref;
          demangle_path (rdm, in_value);
          rdm->next = old_next;
        }
      break;
    default:
      ERROR_AND (return )
    }
}

static void
demangle_generic_arg (struct rust_demangler *rdm)
{
  uint64_t lt;
  if (eat (rdm, 'L'))
    {
      lt = parse_integer_62 (rdm);
      print_lifetime_from_index (rdm, lt);
    }
  else if (eat (rdm, 'K'))
    demangle_const (rdm);
  else
    demangle_type (rdm);
}

static const char *
basic_type (char tag)
{
  switch (tag)
    {
    case 'b':
      return "bool";
    case 'c':
      return "char";
    case 'e':
      return "str";
    case 'u':
      return "()";
    case 'a':
      return "i8";
    case 's':
      return "i16";
    case 'l':
      return "i32";
    case 'x':
      return "i64";
    case 'n':
      return "i128";
    case 'i':
      return "isize";
    case 'h':
      return "u8";
    case 't':
      return "u16";
    case 'm':
      return "u32";
    case 'y':
      return "u64";
    case 'o':
      return "u128";
    case 'j':
      return "usize";
    case 'f':
      return "f32";
    case 'd':
      return "f64";
    case 'z':
      return "!";
    case 'p':
      return "_";
    case 'v':
      return "...";

    default:
      return NULL;
    }
}

static void
demangle_type (struct rust_demangler *rdm)
{
  char tag;
  size_t i, old_next, backref;
  uint64_t lt, old_bound_lifetime_depth;
  const char *basic;
  struct rust_mangled_ident abi;

  CHECK_OR (!rdm->errored, return );

  tag = next (rdm);

  basic = basic_type (tag);
  if (basic)
    {
      PRINT (basic);
      return;
    }

  switch (tag)
    {
    case 'R':
    case 'Q':
      PRINT ("&");
      if (eat (rdm, 'L'))
        {
          lt = parse_integer_62 (rdm);
          if (lt)
            {
              print_lifetime_from_index (rdm, lt);
              PRINT (" ");
            }
        }
      if (tag != 'R')
        PRINT ("mut ");
      demangle_type (rdm);
      break;
    case 'P':
    case 'O':
      PRINT ("*");
      if (tag != 'P')
        PRINT ("mut ");
      else
        PRINT ("const ");
      demangle_type (rdm);
      break;
    case 'A':
    case 'S':
      PRINT ("[");
      demangle_type (rdm);
      if (tag == 'A')
        {
          PRINT ("; ");
          demangle_const (rdm);
        }
      PRINT ("]");
      break;
    case 'T':
      PRINT ("(");
      for (i = 0; !rdm->errored && !eat (rdm, 'E'); i++)
        {
          if (i > 0)
            PRINT (", ");
          demangle_type (rdm);
        }
      if (i == 1)
        PRINT (",");
      PRINT (")");
      break;
    case 'F':
      old_bound_lifetime_depth = rdm->bound_lifetime_depth;
      demangle_binder (rdm);

      if (eat (rdm, 'U'))
        PRINT ("unsafe ");

      if (eat (rdm, 'K'))
        {
          if (eat (rdm, 'C'))
            {
              abi.ascii = "C";
              abi.ascii_len = 1;
            }
          else
            {
              abi = parse_ident (rdm);
              CHECK_OR (abi.ascii && !abi.punycode, goto restore);
            }

          PRINT ("extern \"");

          /* If the ABI had any `-`, they were replaced with `_`,
             so the parts between `_` have to be re-joined with `-`. */
          for (i = 0; i < abi.ascii_len; i++)
            {
              if (abi.ascii[i] == '_')
                {
                  print_str (rdm, abi.ascii, i);
                  PRINT ("-");
                  abi.ascii += i + 1;
                  abi.ascii_len -= i + 1;
                  i = 0;
                }
            }
          print_str (rdm, abi.ascii, abi.ascii_len);

          PRINT ("\" ");
        }

      PRINT ("fn(");
      for (i = 0; !rdm->errored && !eat (rdm, 'E'); i++)
        {
          if (i > 0)
            PRINT (", ");
          demangle_type (rdm);
        }
      PRINT (")");

      if (eat (rdm, 'u'))
        {
          /* Skip printing the return type if it's 'u', i.e. `()`. */
        }
      else
        {
          PRINT (" -> ");
          demangle_type (rdm);
        }

    /* Restore `bound_lifetime_depth` to outside the binder. */
    restore:
      rdm->bound_lifetime_depth = old_bound_lifetime_depth;
      break;
    case 'D':
      PRINT ("dyn ");

      old_bound_lifetime_depth = rdm->bound_lifetime_depth;
      demangle_binder (rdm);

      for (i = 0; !rdm->errored && !eat (rdm, 'E'); i++)
        {
          if (i > 0)
            PRINT (" + ");
          demangle_dyn_trait (rdm);
        }

      /* Restore `bound_lifetime_depth` to outside the binder. */
      rdm->bound_lifetime_depth = old_bound_lifetime_depth;

      CHECK_OR (eat (rdm, 'L'), return );
      lt = parse_integer_62 (rdm);
      if (lt)
        {
          PRINT (" + ");
          print_lifetime_from_index (rdm, lt);
        }
      break;
    case 'B':
      backref = parse_integer_62 (rdm);
      if (!rdm->skipping_printing)
        {
          old_next = rdm->next;
          rdm->next = backref;
          demangle_type (rdm);
          rdm->next = old_next;
        }
      break;
    default:
      /* Go back to the tag, so `demangle_path` also sees it. */
      rdm->next--;
      demangle_path (rdm, 0);
    }
}

/* A trait in a trait object may have some "existential projections"
   (i.e. associated type bindings) after it, which should be printed
   in the `<...>` of the trait, e.g. `dyn Trait<T, U, Assoc=X>`.
   To this end, this method will keep the `<...>` of an 'I' path
   open, by omitting the `>`, and return `Ok(true)` in that case. */
static int
demangle_path_maybe_open_generics (struct rust_demangler *rdm)
{
  int open;
  size_t i, old_next, backref;

  open = 0;

  CHECK_OR (!rdm->errored, return open);

  if (eat (rdm, 'B'))
    {
      backref = parse_integer_62 (rdm);
      if (!rdm->skipping_printing)
        {
          old_next = rdm->next;
          rdm->next = backref;
          open = demangle_path_maybe_open_generics (rdm);
          rdm->next = old_next;
        }
    }
  else if (eat (rdm, 'I'))
    {
      demangle_path (rdm, 0);
      PRINT ("<");
      open = 1;
      for (i = 0; !rdm->errored && !eat (rdm, 'E'); i++)
        {
          if (i > 0)
            PRINT (", ");
          demangle_generic_arg (rdm);
        }
    }
  else
    demangle_path (rdm, 0);
  return open;
}

static void
demangle_dyn_trait (struct rust_demangler *rdm)
{
  int open;
  struct rust_mangled_ident name;

  CHECK_OR (!rdm->errored, return );

  open = demangle_path_maybe_open_generics (rdm);

  while (eat (rdm, 'p'))
    {
      if (!open)
        PRINT ("<");
      else
        PRINT (", ");
      open = 1;

      name = parse_ident (rdm);
      print_ident (rdm, name);
      PRINT ("=");
      demangle_type (rdm);
    }

  if (open)
    PRINT (">");
}

static void
demangle_const (struct rust_demangler *rdm)
{
  char ty_tag;
  size_t old_next, backref;

  CHECK_OR (!rdm->errored, return );

  if (eat (rdm, 'B'))
    {
      backref = parse_integer_62 (rdm);
      if (!rdm->skipping_printing)
        {
          old_next = rdm->next;
          rdm->next = backref;
          demangle_const (rdm);
          rdm->next = old_next;
        }
      return;
    }

  ty_tag = next (rdm);
  switch (ty_tag)
    {
    /* Unsigned integer types. */
    case 'h':
    case 't':
    case 'm':
    case 'y':
    case 'o':
    case 'j':
      break;

    default:
      ERROR_AND (return );
    }

  if (eat (rdm, 'p'))
    PRINT ("_");
  else
    demangle_const_uint (rdm);

  if (rdm->verbose)
    {
      PRINT (": ");
      PRINT (basic_type (ty_tag));
    }
}

static void
demangle_const_uint (struct rust_demangler *rdm)
{
  char c;
  size_t hex_len;
  uint64_t value;

  CHECK_OR (!rdm->errored, return );

  value = 0;
  hex_len = 0;
  while (!eat (rdm, '_'))
    {
      value <<= 4;

      c = next (rdm);
      if (IS_DIGIT (c))
        value |= c - '0';
      else if (IS_LOWER (c))
        value |= 10 + (c - 'a');
      else
        ERROR_AND (return );
      hex_len++;
    }

  /* Print anything that doesn't fit in `uint64_t` verbatim. */
  if (hex_len > 16)
    {
      PRINT ("0x");
      print_str (rdm, rdm->sym + (rdm->next - hex_len), hex_len);
      return;
    }

  print_uint64 (rdm, value);
}

int
rust_demangle_with_callback (const char *mangled, void *callback_opaque,
                             rust_demangler_callback callback)
{
  const char *p;
  struct rust_demangler rdm;

  /* Rust symbols always start with _R. */
  if (mangled[0] == '_' && mangled[1] == 'R')
    mangled += 2;
  else
    return 0;

  /* Paths always start with uppercase characters. */
  if (!IS_UPPER (mangled[0]))
    return 0;

  rdm.sym = mangled;
  rdm.sym_len = 0;

  rdm.callback_opaque = callback_opaque;
  rdm.callback = callback;

  rdm.next = 0;
  rdm.errored = 0;
  rdm.skipping_printing = 0;
  rdm.verbose = 0;
  rdm.bound_lifetime_depth = 0;

  /* Rust symbols use only [_0-9a-zA-Z] characters. */
  for (p = mangled; *p; p++)
    {
      if (!(*p == '_' || IS_DIGIT (*p) || IS_LOWER (*p) || IS_UPPER (*p)))
        return 0;
      rdm.sym_len++;
    }

  demangle_path (&rdm, 1);

  /* Skip instantiating crate. */
  if (!rdm.errored && rdm.next < rdm.sym_len)
    {
      rdm.skipping_printing = 1;
      demangle_path (&rdm, 0);
    }

  /* It's an error to not reach the end. */
  rdm.errored |= rdm.next != rdm.sym_len;

  return !rdm.errored;
}

/* Growable string buffers. */
struct str_buf
{
  char *ptr;
  size_t len;
  size_t cap;
  int errored;
};

static void
str_buf_reserve (struct str_buf *buf, size_t extra)
{
  size_t available, min_new_cap, new_cap;
  char *new_ptr;

  /* Allocation failed before. */
  if (buf->errored)
    return;

  available = buf->cap - buf->len;

  if (extra <= available)
    return;

  min_new_cap = buf->cap + (extra - available);

  /* Check for overflows. */
  if (min_new_cap < buf->cap)
    {
      buf->errored = 1;
      return;
    }

  new_cap = buf->cap;

  if (new_cap == 0)
    new_cap = 4;

  /* Double capacity until sufficiently large. */
  while (new_cap < min_new_cap)
    {
      new_cap *= 2;

      /* Check for overflows. */
      if (new_cap < buf->cap)
        {
          buf->errored = 1;
          return;
        }
    }

  new_ptr = (char *)realloc (buf->ptr, new_cap);
  if (new_ptr == NULL)
    {
      free (buf->ptr);
      buf->ptr = NULL;
      buf->len = 0;
      buf->cap = 0;
      buf->errored = 1;
    }
  else
    {
      buf->ptr = new_ptr;
      buf->cap = new_cap;
    }
}

static void
str_buf_append (struct str_buf *buf, const char *data, size_t len)
{
  str_buf_reserve (buf, len);
  if (buf->errored)
    return;

  memcpy (buf->ptr + buf->len, data, len);
  buf->len += len;
}

char *
rust_demangle (const char *mangled)
{
  struct str_buf out;
  int success;

  out.ptr = NULL;
  out.len = 0;
  out.cap = 0;
  out.errored = 0;

  success = rust_demangle_with_callback (
      mangled, &out, (rust_demangler_callback)str_buf_append);

  if (!success)
    {
      free (out.ptr);
      return NULL;
    }

  str_buf_append (&out, "\0", 1);
  return out.ptr;
}
