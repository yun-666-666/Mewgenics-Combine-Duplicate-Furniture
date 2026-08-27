#pragma once

#ifdef __cplusplus
extern "C" {
#endif

int cdf_prompt_init(void);
void cdf_prompt_block_input(int blocked);
void* cdf_prompt_house(void);
void* cdf_prompt_find(void* scene, void** owner);
void* cdf_prompt_child(void* root, const char* name);
int cdf_prompt_text(void* node, const char* text);
int cdf_prompt_frame(void* node, int frame);

#ifdef __cplusplus
}
#endif
