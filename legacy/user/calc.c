#include "types.h"
#include "ulib.h"

#define FRAC_SCALE 100000000U
#define FRAC_DIGITS 8
#define MAX_WHOLE 99999999U

typedef struct fixed
{
    int negative;
    uint32_t whole;
    uint32_t frac;
} fixed_t;

typedef struct parser
{
    const char *input;
    int position;
    int has_error;
} parser_t;

/* Avoid relying on the parser pointer stack slot inside deep fixed_* calls. */
static parser_t *active_parser;
static parser_t g_parser;
static char g_expression[64];

static void fixed_set_zero(fixed_t *value)
{
    value->negative = 0;
    value->whole = 0;
    value->frac = 0;
}

static void fixed_set_uint(fixed_t *value, uint32_t whole)
{
    value->negative = 0;
    value->whole = whole;
    value->frac = 0;
}

static int fixed_is_zero(const fixed_t *value)
{
    return value->whole == 0 && value->frac == 0;
}

static void skip_spaces(parser_t *parser)
{
    if (parser == NULL || parser->input == NULL)
    {
        return;
    }

    while (parser->input[parser->position] == ' ')
    {
        parser->position++;
    }
}

static char peek(parser_t *parser)
{
    skip_spaces(parser);
    return parser->input[parser->position];
}

static char consume(parser_t *parser)
{
    skip_spaces(parser);
    return parser->input[parser->position++];
}

static void parse_expr(parser_t *parser, fixed_t *out);
static void parse_term(parser_t *parser, fixed_t *out);
static void parse_power(parser_t *parser, fixed_t *out);
static void parse_unary(parser_t *parser, fixed_t *out);
static void parse_primary(parser_t *parser, fixed_t *out);

static void mul_u32(uint32_t left, uint32_t right, uint32_t *hi, uint32_t *lo)
{
    uint32_t left_low = left & 0xFFFFU;
    uint32_t left_high = left >> 16;
    uint32_t right_low = right & 0xFFFFU;
    uint32_t right_high = right >> 16;
    uint32_t ll = left_low * right_low;
    uint32_t lh = left_low * right_high;
    uint32_t hl = left_high * right_low;
    uint32_t hh = left_high * right_high;
    uint32_t mid = (ll >> 16) + (lh & 0xFFFFU) + (hl & 0xFFFFU);

    *lo = (ll & 0xFFFFU) | (mid << 16);
    *hi = hh + (lh >> 16) + (hl >> 16) + (mid >> 16);
}

static int div_u64_by_u32(
    uint32_t hi,
    uint32_t lo,
    uint32_t divisor,
    uint32_t *q_hi,
    uint32_t *q_lo,
    uint32_t *remainder
)
{
    uint32_t quotient_high = 0;
    uint32_t quotient_low = 0;
    uint32_t rem = 0;
    int bit;

    if (divisor == 0)
    {
        return 0;
    }

    for (bit = 0; bit < 64; bit++)
    {
        rem <<= 1;

        if (hi & 0x80000000U)
        {
            rem |= 1U;
        }

        hi = (hi << 1) | (lo >> 31);
        lo <<= 1;
        quotient_high = (quotient_high << 1) | (quotient_low >> 31);
        quotient_low <<= 1;

        if (rem >= divisor)
        {
            rem -= divisor;
            quotient_low |= 1U;
        }
    }

    *q_hi = quotient_high;
    *q_lo = quotient_low;
    *remainder = rem;
    return 1;
}

static int ptr_in_user_stack(const void *pointer)
{
    uint32_t address = (uint32_t)pointer;

    /* User image (code/rodata/bss) or user stack. */
    if (address >= 0x00200000U && address < 0x00210000U)
    {
        return 1;
    }

    if (address >= 0x00400000U && address < 0x00404000U)
    {
        return 1;
    }

    return 0;
}

static void parser_set_error(parser_t *parser)
{
    if (parser == NULL)
    {
        parser = active_parser;
    }

    if (parser == NULL)
    {
        parser = &g_parser;
    }

    parser->has_error = 1;
}

static void fixed_normalize(
    parser_t *parser,
    fixed_t *out,
    int negative,
    uint32_t whole,
    uint32_t frac
)
{
    (void)parser;
    parser = active_parser;

    if (out == NULL || !ptr_in_user_stack(out))
    {
        parser_set_error(parser);
        return;
    }

    if (frac >= FRAC_SCALE)
    {
        whole += frac / FRAC_SCALE;
        frac %= FRAC_SCALE;
    }

    if (whole > MAX_WHOLE)
    {
        parser_set_error(parser);
        fixed_set_zero(out);
        return;
    }

    if (whole == 0 && frac == 0)
    {
        negative = 0;
    }

    out->negative = negative ? 1 : 0;
    out->whole = whole;
    out->frac = frac;
}

static int fixed_cmp_mag(const fixed_t *left, const fixed_t *right)
{
    if (left->whole < right->whole)
    {
        return -1;
    }

    if (left->whole > right->whole)
    {
        return 1;
    }

    if (left->frac < right->frac)
    {
        return -1;
    }

    if (left->frac > right->frac)
    {
        return 1;
    }

    return 0;
}

static void fixed_add(
    parser_t *parser,
    fixed_t *out,
    const fixed_t *left,
    const fixed_t *right
)
{
    uint32_t whole;
    uint32_t frac;
    int negative;
    fixed_t left_value;
    fixed_t right_value;

    (void)parser;
    parser = active_parser;

    if (parser == NULL ||
        out == NULL ||
        left == NULL ||
        right == NULL ||
        !ptr_in_user_stack(out) ||
        !ptr_in_user_stack(left) ||
        !ptr_in_user_stack(right))
    {
        parser_set_error(parser);
        if (out != NULL && ptr_in_user_stack(out))
        {
            fixed_set_zero(out);
        }
        return;
    }

    left_value = *left;
    right_value = *right;

    if (left_value.negative == right_value.negative)
    {
        frac = left_value.frac + right_value.frac;
        whole = left_value.whole + right_value.whole;

        if (frac >= FRAC_SCALE)
        {
            frac -= FRAC_SCALE;
            whole++;
        }

        if (whole < left_value.whole || whole > MAX_WHOLE)
        {
            parser_set_error(parser);
            fixed_set_zero(out);
            return;
        }

        fixed_normalize(parser, out, left_value.negative, whole, frac);
        return;
    }

    if (left_value.whole > right_value.whole ||
        (left_value.whole == right_value.whole &&
         left_value.frac >= right_value.frac))
    {
        negative = left_value.negative;

        if (left_value.frac >= right_value.frac)
        {
            frac = left_value.frac - right_value.frac;
            whole = left_value.whole - right_value.whole;
        }
        else
        {
            frac = left_value.frac + FRAC_SCALE - right_value.frac;
            whole = left_value.whole - right_value.whole - 1U;
        }

        fixed_normalize(parser, out, negative, whole, frac);
        return;
    }

    negative = right_value.negative;

    if (right_value.frac >= left_value.frac)
    {
        frac = right_value.frac - left_value.frac;
        whole = right_value.whole - left_value.whole;
    }
    else
    {
        frac = right_value.frac + FRAC_SCALE - left_value.frac;
        whole = right_value.whole - left_value.whole - 1U;
    }

    fixed_normalize(parser, out, negative, whole, frac);
}

static void fixed_sub(
    parser_t *parser,
    fixed_t *out,
    const fixed_t *left,
    const fixed_t *right
)
{
    fixed_t negated;

    (void)parser;
    parser = active_parser;

    if (parser == NULL || out == NULL || left == NULL || right == NULL)
    {
        parser_set_error(parser);
        return;
    }

    negated = *right;

    negated.negative = !negated.negative;

    if (fixed_is_zero(&negated))
    {
        negated.negative = 0;
    }

    fixed_add(parser, out, left, &negated);
}

static void fixed_mul(
    parser_t *parser,
    fixed_t *out,
    const fixed_t *left,
    const fixed_t *right
)
{
    uint32_t hi;
    uint32_t lo;
    uint32_t q_hi;
    uint32_t q_lo;
    uint32_t rem;
    uint32_t whole = 0;
    uint32_t frac = 0;
    uint32_t carry = 0;
    int negative;

    (void)parser;
    parser = active_parser;

    if (parser == NULL || out == NULL || left == NULL || right == NULL)
    {
        parser_set_error(parser);
        return;
    }

    negative = left->negative ^ right->negative;

    mul_u32(left->whole, right->whole, &hi, &lo);

    if (hi != 0 || lo > MAX_WHOLE)
    {
        parser_set_error(parser);
        fixed_set_zero(out);
        return;
    }

    whole = lo;

    mul_u32(left->whole, right->frac, &hi, &lo);

    if (!div_u64_by_u32(hi, lo, FRAC_SCALE, &q_hi, &q_lo, &rem) ||
        q_hi != 0 ||
        q_lo > MAX_WHOLE - whole)
    {
        parser_set_error(parser);
        fixed_set_zero(out);
        return;
    }

    whole += q_lo;
    frac = rem;

    mul_u32(right->whole, left->frac, &hi, &lo);

    if (!div_u64_by_u32(hi, lo, FRAC_SCALE, &q_hi, &q_lo, &rem) ||
        q_hi != 0 ||
        q_lo > MAX_WHOLE - whole)
    {
        parser_set_error(parser);
        fixed_set_zero(out);
        return;
    }

    whole += q_lo;
    frac += rem;

    if (frac >= FRAC_SCALE)
    {
        frac -= FRAC_SCALE;
        carry = 1;
    }

    if (carry > MAX_WHOLE - whole)
    {
        parser_set_error(parser);
        fixed_set_zero(out);
        return;
    }

    whole += carry;

    mul_u32(left->frac, right->frac, &hi, &lo);

    if (!div_u64_by_u32(hi, lo, FRAC_SCALE, &q_hi, &q_lo, &rem) || q_hi != 0)
    {
        parser_set_error(parser);
        fixed_set_zero(out);
        return;
    }

    frac += q_lo;

    if (frac >= FRAC_SCALE)
    {
        frac -= FRAC_SCALE;
        whole++;
    }

    if (whole > MAX_WHOLE)
    {
        parser_set_error(parser);
        fixed_set_zero(out);
        return;
    }

    fixed_normalize(parser, out, negative, whole, frac);
}

static void fixed_mul_10(parser_t *parser, fixed_t *value)
{
    uint32_t whole;
    uint32_t frac;

    if (value->whole > MAX_WHOLE / 10U)
    {
        parser_set_error(parser);
        fixed_set_zero(value);
        return;
    }

    whole = value->whole * 10U + value->frac / (FRAC_SCALE / 10U);
    frac = (value->frac % (FRAC_SCALE / 10U)) * 10U;

    if (whole > MAX_WHOLE)
    {
        parser_set_error(parser);
        fixed_set_zero(value);
        return;
    }

    fixed_normalize(parser, value, value->negative, whole, frac);
}

static void fixed_sub_mag(
    parser_t *parser,
    fixed_t *out,
    const fixed_t *left,
    const fixed_t *right
)
{
    uint32_t whole;
    uint32_t frac;

    if (fixed_cmp_mag(left, right) < 0)
    {
        parser_set_error(parser);
        fixed_set_zero(out);
        return;
    }

    if (left->frac >= right->frac)
    {
        frac = left->frac - right->frac;
        whole = left->whole - right->whole;
    }
    else
    {
        frac = left->frac + FRAC_SCALE - right->frac;
        whole = left->whole - right->whole - 1U;
    }

    fixed_normalize(parser, out, 0, whole, frac);
}

static void fixed_div(
    parser_t *parser,
    fixed_t *out,
    const fixed_t *left,
    const fixed_t *right
)
{
    fixed_t remainder;
    fixed_t divisor;
    uint32_t whole = 0;
    uint32_t frac = 0;
    uint32_t place = FRAC_SCALE / 10U;
    int negative;

    (void)parser;
    parser = active_parser;

    if (parser == NULL || out == NULL || left == NULL || right == NULL)
    {
        parser_set_error(parser);
        return;
    }

    negative = left->negative ^ right->negative;

    if (fixed_is_zero(right))
    {
        parser_set_error(parser);
        fixed_set_zero(out);
        return;
    }

    remainder = *left;
    remainder.negative = 0;
    divisor = *right;
    divisor.negative = 0;

    while (fixed_cmp_mag(&remainder, &divisor) >= 0)
    {
        fixed_t multiple = divisor;
        uint32_t add = 1;

        for (;;)
        {
            fixed_t doubled;
            uint32_t doubled_whole;
            uint32_t doubled_frac;

            if (multiple.whole > (MAX_WHOLE / 2U) || add > (MAX_WHOLE / 2U))
            {
                break;
            }

            doubled_frac = multiple.frac * 2U;
            doubled_whole = multiple.whole * 2U;

            if (doubled_frac >= FRAC_SCALE)
            {
                doubled_frac -= FRAC_SCALE;
                doubled_whole++;
            }

            if (doubled_whole > MAX_WHOLE)
            {
                break;
            }

            fixed_normalize(parser, &doubled, 0, doubled_whole, doubled_frac);

            if (fixed_cmp_mag(&remainder, &doubled) < 0)
            {
                break;
            }

            multiple = doubled;
            add *= 2U;
        }

        fixed_sub_mag(parser, &remainder, &remainder, &multiple);

        if (parser->has_error || whole > MAX_WHOLE - add)
        {
            parser_set_error(parser);
            fixed_set_zero(out);
            return;
        }

        whole += add;
    }

    while (place > 0 && !fixed_is_zero(&remainder))
    {
        uint32_t digit = 0;

        fixed_mul_10(parser, &remainder);

        if (parser->has_error)
        {
            fixed_set_zero(out);
            return;
        }

        while (fixed_cmp_mag(&remainder, &divisor) >= 0)
        {
            fixed_sub_mag(parser, &remainder, &remainder, &divisor);
            digit++;

            if (parser->has_error || digit > 9U)
            {
                parser_set_error(parser);
                fixed_set_zero(out);
                return;
            }
        }

        frac += digit * place;
        place /= 10U;
    }

    fixed_normalize(parser, out, negative, whole, frac);
}

static void fixed_pow(
    parser_t *parser,
    fixed_t *out,
    const fixed_t *base,
    const fixed_t *exponent
)
{
    fixed_t result;
    uint32_t power;
    uint32_t index;

    if (exponent->frac != 0 || exponent->negative)
    {
        parser_set_error(parser);
        fixed_set_zero(out);
        return;
    }

    fixed_set_uint(&result, 1);
    power = exponent->whole;

    for (index = 0; index < power; index++)
    {
        fixed_t next;

        fixed_mul(parser, &next, &result, base);

        if (parser->has_error)
        {
            fixed_set_zero(out);
            return;
        }

        result = next;
    }

    *out = result;
}

static void fixed_factorial(parser_t *parser, fixed_t *out, const fixed_t *value)
{
    fixed_t result;
    uint32_t number;

    if (value->frac != 0 || value->negative)
    {
        parser_set_error(parser);
        fixed_set_zero(out);
        return;
    }

    fixed_set_uint(&result, 1);
    number = value->whole;

    while (number > 1)
    {
        fixed_t factor;
        fixed_t next;

        fixed_set_uint(&factor, number);
        fixed_mul(parser, &next, &result, &factor);

        if (parser->has_error)
        {
            fixed_set_zero(out);
            return;
        }

        result = next;
        number--;
    }

    *out = result;
}

static void parse_number(parser_t *parser, fixed_t *out)
{
    uint32_t whole = 0;
    uint32_t frac = 0;
    uint32_t place = FRAC_SCALE / 10U;
    char current = peek(parser);
    int saw_digit = 0;

    while (current >= '0' && current <= '9')
    {
        uint32_t digit = (uint32_t)(current - '0');

        saw_digit = 1;

        if (whole > (MAX_WHOLE - digit) / 10U)
        {
            parser_set_error(parser);
            fixed_set_zero(out);
            return;
        }

        whole = whole * 10U + digit;
        parser->position++;
        current = parser->input[parser->position];
    }

    if (current == '.')
    {
        parser->position++;
        current = parser->input[parser->position];

        while (place > 0 && current >= '0' && current <= '9')
        {
            saw_digit = 1;
            frac += (uint32_t)(current - '0') * place;
            place /= 10U;
            parser->position++;
            current = parser->input[parser->position];
        }
    }

    if (!saw_digit)
    {
        parser_set_error(parser);
        fixed_set_zero(out);
        return;
    }

    fixed_normalize(parser, out, 0, whole, frac);
}

static void parse_primary(parser_t *parser, fixed_t *out)
{
    char current = peek(parser);

    if ((current >= '0' && current <= '9') || current == '.')
    {
        parse_number(parser, out);
        return;
    }

    if (current == '(')
    {
        consume(parser);
        parse_expr(parser, out);

        if (consume(parser) != ')')
        {
            parser_set_error(parser);
        }

        return;
    }

    parser_set_error(parser);
    fixed_set_zero(out);
}

static void parse_unary(parser_t *parser, fixed_t *out)
{
    char current = peek(parser);

    if (current == '-')
    {
        consume(parser);
        parse_unary(parser, out);

        if (!fixed_is_zero(out))
        {
            out->negative = !out->negative;
        }

        return;
    }

    if (current == '+')
    {
        consume(parser);
        parse_unary(parser, out);
        return;
    }

    parse_primary(parser, out);
    skip_spaces(parser);

    if (peek(parser) == '!')
    {
        fixed_t value = *out;

        consume(parser);
        fixed_factorial(parser, out, &value);
    }
}

static void parse_power(parser_t *parser, fixed_t *out)
{
    fixed_t base;

    parse_unary(parser, &base);
    skip_spaces(parser);

    if (peek(parser) == '^')
    {
        fixed_t exponent;

        consume(parser);
        parse_power(parser, &exponent);
        fixed_pow(parser, out, &base, &exponent);
        return;
    }

    *out = base;
}

static void parse_term(parser_t *parser, fixed_t *out)
{
    fixed_t left;
    char operator;

    parse_power(parser, &left);

    for (;;)
    {
        fixed_t right;
        fixed_t next;

        skip_spaces(parser);
        operator = peek(parser);

        if (operator != '*' && operator != '/')
        {
            break;
        }

        consume(parser);
        parse_power(parser, &right);

        if (operator == '*')
        {
            fixed_mul(parser, &next, &left, &right);
        }
        else
        {
            fixed_div(parser, &next, &left, &right);
        }

        if (parser->has_error)
        {
            fixed_set_zero(out);
            return;
        }

        left = next;
    }

    *out = left;
}

static void parse_expr(parser_t *parser, fixed_t *out)
{
    fixed_t left;
    char operator;

    parse_term(parser, &left);

    for (;;)
    {
        fixed_t right;
        fixed_t next;

        skip_spaces(parser);
        operator = peek(parser);

        if (operator != '+' && operator != '-')
        {
            break;
        }

        consume(parser);
        parse_term(parser, &right);

        if (operator == '+')
        {
            fixed_add(parser, &next, &left, &right);
        }
        else
        {
            fixed_sub(parser, &next, &left, &right);
        }

        if (parser->has_error)
        {
            fixed_set_zero(out);
            return;
        }

        left = next;
    }

    *out = left;
}

static void print_fixed(const fixed_t *value)
{
    char text[24];
    uint32_t place = FRAC_SCALE / 10U;
    uint32_t frac = value->frac;
    uint32_t whole = value->whole;
    int out = 0;
    int start;
    int end;
    char digits[10];
    int count = 0;

    if (value->negative)
    {
        text[out++] = '-';
    }

    if (whole == 0)
    {
        text[out++] = '0';
    }
    else
    {
        while (whole > 0)
        {
            digits[count++] = (char)('0' + (whole % 10U));
            whole /= 10U;
        }

        while (count > 0)
        {
            text[out++] = digits[--count];
        }
    }

    if (frac != 0)
    {
        text[out++] = '.';
        start = out;

        while (place > 0)
        {
            text[out++] = (char)('0' + (frac / place));
            frac %= place;
            place /= 10U;
        }

        end = out;

        while (end > start && text[end - 1] == '0')
        {
            end--;
        }

        out = end;
    }

    text[out] = '\0';
    uwrite(text);
}

static void build_expression(int argc, char **argv, char *buffer, int buffer_size)
{
    int output = 0;
    int index;

    buffer[0] = '\0';

    for (index = 1; index < argc; index++)
    {
        const char *part = argv[index];
        int part_index = 0;

        if (index > 1 && output + 1 < buffer_size)
        {
            buffer[output++] = ' ';
        }

        while (part[part_index] != '\0' && output + 1 < buffer_size)
        {
            buffer[output++] = part[part_index++];
        }
    }

    buffer[output] = '\0';
}

int main(int argc, char **argv)
{
    fixed_t result;

    if (argc < 2)
    {
        uwrite("Usage: calc <expression>\n");
        uwrite("Example: calc 2+3*4\n");
        uwrite("         calc 3.14*2\n");
        uwrite("         calc 10/4\n");
        uexit(1);
    }

    build_expression(argc, argv, g_expression, (int)sizeof(g_expression));

    g_parser.input = g_expression;
    g_parser.position = 0;
    g_parser.has_error = 0;
    active_parser = &g_parser;
    parse_expr(&g_parser, &result);
    active_parser = 0;

    if (g_parser.has_error || peek(&g_parser) != '\0')
    {
        uwrite("Error.\n");
        uexit(1);
    }

    print_fixed(&result);
    uwrite("\n");
    uexit(0);
    return 0;
}
