#include "jzon.h"
#include <stdlib.h>
#include <string.h>


// String helpers

typedef struct String
{
	int size;
	int capacity;
	char* str;
} String;

void str_grow(String* str, JzonAllocator* allocator)
{
	int new_capacity = str->capacity == 0 ? 2 : str->capacity * 2;
	char* new_str = (char*)allocator->allocate(new_capacity);
	
	if (str->str != NULL)
		strcpy(new_str, str->str);
	
	allocator->deallocate(str->str);
	str->str = new_str;
	str->capacity = new_capacity;
}

void str_add(String* str, char c, JzonAllocator* allocator)
{
	if (str->size + 1 >= str->capacity)
		str_grow(str, allocator);

	str->str[str->size] = c;
	str->str[str->size + 1] = '\0';
	++str->size;
}

bool str_equals(String* str, char* other)
{
	return strcmp(str->str, other) == 0;
}


// Array helpers

typedef struct Array
{
	int size;
	int capacity;
	void** arr;
} Array;

void arr_grow(Array* arr, JzonAllocator* allocator)
{
	int new_capacity = arr->capacity == 0 ? 1 : arr->capacity * 2;
	void** new_arr = (void**)allocator->allocate(new_capacity * sizeof(void**));
	memcpy(new_arr, arr->arr, arr->size * sizeof(void**));
	allocator->deallocate(arr->arr);
	arr->arr = new_arr;
	arr->capacity = new_capacity;
}

void arr_add(Array* arr, void* e, JzonAllocator* allocator)
{
	if (arr->size == arr->capacity)
		arr_grow(arr, allocator);

	arr->arr[arr->size] = e;
	++arr->size;
}

void arr_insert(Array* arr, void* e, unsigned index, JzonAllocator* allocator)
{
	if (arr->size == arr->capacity)
		arr_grow(arr, allocator);

	memmove(arr->arr + index + 1, arr->arr + index, (arr->size - index) * sizeof(void**));
	arr->arr[index] = e;
	++arr->size;
}


// Hash function used for hashing object keys.
// From http://murmurhash.googlepages.com/

uint64_t hash_str(const char* str, size_t len)
{
	uint64_t seed = 0;

	const uint64_t m = 0xc6a4a7935bd1e995ULL;
	const uint32_t r = 47;

	uint64_t h = seed ^ (len * m);

	const uint64_t * data = (const uint64_t *)str;
	const uint64_t * end = data + (len / 8);

	while (data != end)
	{
#ifdef PLATFORM_BIG_ENDIAN
		uint64_t k = *data++;
		char *p = (char *)&k;
		char c;
		c = p[0]; p[0] = p[7]; p[7] = c;
		c = p[1]; p[1] = p[6]; p[6] = c;
		c = p[2]; p[2] = p[5]; p[5] = c;
		c = p[3]; p[3] = p[4]; p[4] = c;
#else
		uint64_t k = *data++;
#endif

		k *= m;
		k ^= k >> r;
		k *= m;

		h ^= k;
		h *= m;
	}

	const unsigned char * data2 = (const unsigned char*)data;

	switch (len & 7)
	{
	case 7: h ^= ((uint64_t)data2[6]) << 48;
	case 6: h ^= ((uint64_t)data2[5]) << 40;
	case 5: h ^= ((uint64_t)data2[4]) << 32;
	case 4: h ^= ((uint64_t)data2[3]) << 24;
	case 3: h ^= ((uint64_t)data2[2]) << 16;
	case 2: h ^= ((uint64_t)data2[1]) << 8;
	case 1: h ^= ((uint64_t)data2[0]);
		h *= m;
	};

	h ^= h >> r;
	h *= m;
	h ^= h >> r;

	return h;
}


// OutputBuffer

typedef struct OutputBuffer {
	unsigned size;
	char* data;
	char* write_head;
} OutputBuffer;

void write(OutputBuffer* output, void* data, unsigned size, JzonAllocator* allocator)
{
	unsigned write_head_offset = output->write_head - output->data;

	if (write_head_offset + size > output->size)
	{
		unsigned new_size = output->size * 2 + size;
		char* new_data = allocator->allocate(new_size);
		memcpy(new_data, output->data, output->size);
		allocator->deallocate(output->data);
		output->data = new_data;
		output->write_head = output->data + write_head_offset;
		output->size = new_size;
	}

	memcpy(output->write_head, data, size);
	output->write_head += size;
}

unsigned current_write_offset(OutputBuffer* output)
{
	return output->write_head - output->data;
}

// Jzon implmenetation

__forceinline void next(const char** input)
{
	++*input;
}

__forceinline char current(const char** input)
{
	return **input;
}

bool is_multiline_string_quotes(const char* str)
{
	return *str == '"' && *(str + 1) == '"' && *(str + 1) == '"';
}

/*
int find_object_pair_insertion_index(JzonKeyValuePair** objects, unsigned size, uint64_t key_hash)
{
	if (size == 0)
		return 0;

	for (unsigned i = 0; i < size; ++i)
	{
		if (objects[i]->key_hash > key_hash)
			return i;
	}

	return size;
}*/

void skip_whitespace(const char** input)
{
	while (current(input))
	{
		while (current(input) && (current(input) <= ' ' || current(input) == ','))
			next(input);
		
		// Skip comment.
		if (current(input) == '#')
		{
			while (current(input) && current(input) != '\n')
				next(input);
		}
		else
			break;
	}
};

char* parse_multiline_string(const char** input, JzonAllocator* allocator)
{
	if (!is_multiline_string_quotes(*input))
		return NULL;
	
	*input += 3;
	String str = { 0 };

	while (current(input))
	{
		if (current(input) == '\n' || current(input) == '\r')
		{
			skip_whitespace(input);

			if (str.size > 0)
				str_add(&str, '\n', allocator);
		}

		if (is_multiline_string_quotes(*input))
		{
			*input += 3;
			return str.str;
		}

		str_add(&str, current(input), allocator);
		next(input);
	}

	allocator->deallocate(str.str);
	return NULL;
}

char* parse_string_internal(char** input, OutputBuffer* output, JzonAllocator* allocator)
{
	if (current(input) != '"')
		return NULL;

	/*if (is_multiline_string_quotes(*input))
		return parse_multiline_string(input, allocator);*/

	next(input);

	char* start = *input;
	char* end = start;

	while (current(input))
	{
		if (current(input) == '"')
		{
			next(input);
			break;
		}

		++end;
		next(input);
	}

	unsigned str_len = end - start;
	write(output, start, str_len, allocator);
	char termination = '\0';
	write(output, &termination, sizeof(char), allocator);
}

uint64_t parse_keyname(const char** input, OutputBuffer* output, JzonAllocator* allocator)
{
	if (current(input) == '"')
		return parse_string_internal(input, output, allocator);
		
	char* start = *input;
	char* end = start;

	while (current(input))
	{
		if (current(input) == ':')
			break;
		else
			++end;

		next(input);
	}

	unsigned key_len = end - start;
	write(output, start, key_len, allocator);
	char termination = '\0';
	write(output, &termination, sizeof(char), allocator);

	return hash_str(start, key_len);
}

int parse_value(const char** input, OutputBuffer* output, JzonAllocator* allocator);

int parse_string(const char** input, OutputBuffer* output, JzonAllocator* allocator)
{
	JzonValue* header = (JzonValue*)output->write_head;
	memset(header, 0, sizeof(JzonValue));
	header->is_string = true;
	output->write_head += sizeof(JzonValue);

	parse_string_internal(input, output, allocator);
	return 0;
}

/*
int parse_array(const char** input, JzonValue* output, JzonAllocator* allocator)
{	
	if (current(input) != '[')
		return -1;
	
	output->is_array = true;
	next(input);

	// Empty array.
	if (current(input) == ']')
	{
		output->size = 0; 
		return 0;
	}

	Array array_values = { 0 };

	while (current(input))
	{
		skip_whitespace(input);
		JzonValue* value = (JzonValue*)allocator->allocate(sizeof(JzonValue));
		memset(value, 0, sizeof(JzonValue));
		int error = parse_value(input, value, allocator);

		if (error != 0)
			return error;

		arr_add(&array_values, value, allocator);
		skip_whitespace(input);

		if (current(input) == ']')
		{
			next(input);
			break;
		}
	}
	
	output->size = array_values.size; 
	output->array_values = (JzonValue**)array_values.arr;	
	return 0;
}*/

int parse_object(const char** input, OutputBuffer* output, bool root_object, JzonAllocator* allocator)
{
	if (current(input) == '{')
		next(input);
	else if (!root_object)
		return -1;

	JzonValue* header = (JzonValue*)output->write_head;
	memset(header, 0, sizeof(JzonValue));
	header->is_object = true;
	output->write_head += sizeof(JzonValue);
	unsigned offset_table_offset_write_pos = current_write_offset(output);
	output->write_head += sizeof(unsigned);
	
	// Empty object.
	if (current(input) == '}')
	{
		*(unsigned*)(output->data + offset_table_offset_write_pos) = current_write_offset(output);
		unsigned size = 0;
		write(output, &size, sizeof(size), allocator);
		return 0;
	}

	OutputBuffer offset_table;
	offset_table.data = allocator->allocate(4);
	offset_table.size = 4;
	offset_table.write_head = offset_table.data + sizeof(unsigned);
	*(unsigned*)offset_table.data = 0;

	while (current(input))
	{
		skip_whitespace(input);
		char* key_pos = output->write_head;
		uint64_t key_hash = parse_keyname(input, output, allocator);
		skip_whitespace(input);
		
		if (key_hash == -1 || current(input) != ':')
			return -1;

		next(input);
		char* value_pos = output->write_head;
		int error = parse_value(input, output, allocator);

		if (error != 0)
			return error;

		write(&offset_table, &key_hash, sizeof(uint64_t), allocator);
		unsigned key_offset = (unsigned)(key_pos - output->data);
		write(&offset_table, &key_offset, sizeof(unsigned), allocator);
		unsigned value_offset = (unsigned)(value_pos - output->data);
		write(&offset_table, &value_offset, sizeof(unsigned), allocator);
		++(*(unsigned*)offset_table.data);

		skip_whitespace(input);

		if (current(input) == '}')
		{
			next(input);
			break;
		}
	}
	
	*(unsigned*)(output->data + offset_table_offset_write_pos) = (unsigned)(output->write_head - output->data);
	write(output, offset_table.data, (unsigned)(offset_table.write_head - offset_table.data), allocator);
	allocator->deallocate(offset_table.data);

	return 0;
}

/*
int parse_number(const char** input, JzonValue* output, JzonAllocator* allocator)
{
	String num = {0};
	bool is_float = false;

	if (current(input) == '-')
	{
		str_add(&num, current(input), allocator);
		next(input);
	}

	while (current(input) >= '0' && current(input) <= '9')
	{
		str_add(&num, current(input), allocator);
		next(input);
	}

	if (current(input) == '.')
	{
		is_float = true;
		str_add(&num, current(input), allocator);
		next(input);

		while (current(input) >= '0' && current(input) <= '9')
		{
			str_add(&num, current(input), allocator);
			next(input);
		}
	}

	if (current(input) == 'e' || current(input) == 'E')
	{
		is_float = true;
		str_add(&num, current(input), allocator);
		next(input);

		if (current(input) == '-' || current(input) == '+')
		{
			str_add(&num, current(input), allocator);
			next(input);
		}

		while (current(input) >= '0' && current(input) <= '9')
		{
			str_add(&num, current(input), allocator);
			next(input);
		}
	}

	if (is_float)
	{
		output->is_float = true;
		output->float_value = (float)strtod(num.str, NULL);
	}
	else
	{
		output->is_int = true;
		output->int_value = (int)strtol(num.str, NULL, 10);
	}

	allocator->deallocate(num.str);
	return 0;
}

int parse_word_or_string(const char** input, JzonValue* output, JzonAllocator* allocator)
{
	String str = {0};

	while (current(input))
	{
		if (current(input) == '\r' || current(input) == '\n')
		{
			if (str.size == 4 && str_equals(&str, "true"))
			{
				output->is_bool = true;
				output->bool_value = true;
				allocator->deallocate(str.str);
				return 0;
			}
			else if (str.size == 5 && str_equals(&str, "false"))
			{
				output->is_bool = true;
				output->bool_value = false;
				allocator->deallocate(str.str);
				return 0;
			}
			else if (str.size == 4 && str_equals(&str, "null"))
			{
				output->is_null = true;
				allocator->deallocate(str.str);
				return 0;
			}
			else
			{
				output->is_string = true;
				output->string_value = str.str;
				return 0;
			}

			break;
		}		
		else
			str_add(&str, current(input), allocator);

		next(input);
	}

	allocator->deallocate(str.str);
	return -1;
}*/

int parse_value(const char** input, OutputBuffer* output, JzonAllocator* allocator)
{
	skip_whitespace(input);
	char ch = current(input);

	switch (ch)
	{
		case '{': return parse_object(input, output, false, allocator);
		//case '[': return parse_array(input, output, allocator);
		case '"': return parse_string(input, output, allocator);
		//case '-': return parse_number(input, output, allocator);
		//default: return ch >= '0' && ch <= '9' ? parse_number(input, output, allocator) : parse_word_or_string(input, output, allocator);
	}

	return -1;
}


// Public interface

JzonValue* jzon_parse_custom_allocator(const char* input, JzonAllocator* allocator)
{
	unsigned size = (unsigned)strlen(input);
	char* data = (char*)allocator->allocate(size);
	OutputBuffer output = { size, data, data };
	int error = parse_object(&input, &output, true, allocator);

	if (error != 0)
	{
		allocator->deallocate(output.data);
		return NULL;
	}

	return (JzonValue*)output.data;
}

JzonValue* jzon_parse(const char* input)
{
	JzonAllocator allocator = { malloc, free };
	return jzon_parse_custom_allocator(input, &allocator);
}

typedef struct OffsetTableEntry {
	uint64_t key_hash;
	unsigned key_offset;
	unsigned value_offset;
} OffsetTableEntry;

char* object_table_start(JzonValue* jzon)
{
	return (((char*)jzon) + *(unsigned*)(jzon + 1));
}

unsigned jzon_size(JzonValue* jzon)
{
	if (jzon->is_object)
		return *(unsigned*)object_table_start(jzon);

	return 0;
}

char* jzon_key(JzonValue* jzon, unsigned i)
{
	return (char*)(((char*)jzon) + ((OffsetTableEntry*)(object_table_start(jzon) + sizeof(unsigned)))[i].key_offset);
}

JzonValue* jzon_value(JzonValue* jzon, unsigned i)
{
	return (JzonValue*)(((char*)jzon) + ((OffsetTableEntry*)(object_table_start(jzon) + sizeof(unsigned)))[i].value_offset);
}

char* jzon_string(JzonValue* jzon)
{
	return (char*)(jzon + 1);
}

/*JzonValue* jzon_get(JzonValue* object, const char* key)
{
	if (!object->is_object)
		return NULL;

	if (object->size == 0)
		return NULL;
	
	uint64_t key_hash = hash_str(key);

	unsigned first = 0;
	unsigned last = object->size - 1;
	unsigned middle = (first + last) / 2;

	while (first <= last)
	{
		if (object->object_values[middle]->key_hash < key_hash)
			first = middle + 1;
		else if (object->object_values[middle]->key_hash == key_hash)
			return object->object_values[middle]->value;
		else
			last = middle - 1;

		middle = (first + last) / 2;
	}

	return NULL;
}*/
