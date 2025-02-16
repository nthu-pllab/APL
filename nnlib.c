#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <math.h>
#include <float.h>  //max_pool
#define DEBUG_TIME
#define im2colxGEMM

struct tensor
{
    int n;
    int c;
    int h;
    int w;
    int dim;
    int size;
    float * data;
};

typedef struct tensor tensor;

tensor T[512];
//int N,C,H,W;
//float y;
//float bias, epsilon;
//int padding, stride, groups, size;

float im2col_get_pixel(tensor * im, int height, int width, int channels,
        int row, int col, int channel, int pad)
{
    row -= pad;
    col -= pad;

    if (row < 0 || col < 0 ||
            row >= height || col >= width) return 0;

    return im->data[col + width * (row + height * channel)];
}

int where_pos2(tensor * out, int H, int W) 
{
    return H * out->w + W;
}

int where_pos4(tensor * out, int N, int C, int H, int W) 
{
    int hXw = out->h * out->w;
    return N * out->c * hXw + C * hXw + H * out->w + W;
}

tensor * make_tensor(tensor * out, int n, int c, int h, int w) 
{

    out->n = n;
    out->c = c;
    out->h = h;
    out->w = w;
    out->dim = 4;

    if (n == 0)
    {
        out->n = 1;
        out->dim = 3;
    }

    if (c == 0)
    {
        out->c = 1;
        out->dim = 2;
    }

    if (h == 0)
    {
        out->h = 1;
        out->dim = 1;
    }

    if (w == 0)
    {
        out->w = 1;
        out->dim = 0;
    }
    #define MALLOC_ALIGN    16
    out->size = out->n * out->c * out->h * out->w;
    if (!out->data) posix_memalign((void **)(&(out->data)), MALLOC_ALIGN, out->size  * sizeof(float));
    //if (!out->data) out->data = (float * ) calloc(out->size, sizeof(float));
    return out;
}

void variable(tensor * out, int n, int c, int h, int w, const char * label)
{
	FILE * pFile;
	out = make_tensor(out, n, c, h, w);

	char * file_name = (char *) calloc(strlen(label) + strlen(".dat") + 1, sizeof(char));
	sprintf(file_name, "%s%s", label, ".dat");
    pFile = fopen ( file_name , "rb" );

/*
    string file_name(label);
    file_name = file_name + ".dat";
	pFile = fopen ( file_name.c_str() , "rb" );
*/
	if (pFile==NULL) {fputs ("File error\n",stderr); assert (0);}

	fseek(pFile, 0, SEEK_END);
	int file_size = ftell(pFile);
	fseek(pFile, 0, SEEK_SET);
	//printf("file_size %d\n", file_size);

	char metadata[4];
	fread((char *)metadata, sizeof(unsigned char), 4, pFile);
	unsigned char magic1 = metadata[0];
	unsigned char magic2 = metadata[1];
	unsigned char major  = metadata[2];
	unsigned char minor  = metadata[3];
	//printf("magic1 : %c\n", magic1);

	uint32_t data_length;
	fread((char *)&data_length, sizeof(uint32_t), 1, pFile);
	uint32_t rank;
	fread((char *)&rank, sizeof(uint32_t), 1, pFile);
	uint32_t ranks[4];
	uint32_t *shape = ranks;
	//printf("data_length : %d\n", data_length);
	//printf("rank : %d\n", rank);

	int checkVersion = 1;

	for (int i = 0; i < rank; ++i)
	{
		uint32_t *p = shape + i;
		fread((char *)p, sizeof(uint32_t), 1, pFile);
		checkVersion = checkVersion * (*p);
	}
	//printf("checkVersion : %d\n", checkVersion);

	int header_size = 128;
	int version = 0;
	if (file_size == header_size + data_length && (checkVersion * 4) == data_length)
	{
		assert((file_size - header_size) == (checkVersion * 4));
		//printf("Ver 2.0 - 60ba79d\n");
		version = 2;
	}
	else
	{
		//printf("Ver 1.0 - 02a3916\n");
		version = 1;
	}

	uint8_t code, bits;
	fread((char *)&code, sizeof(uint8_t), 1, pFile);
	fread((char *)&bits, sizeof(uint8_t), 1, pFile);
	uint16_t qlen;
	fread((char *)&qlen, sizeof(uint16_t), 1, pFile);

	uint64_t quantization;
	fread((char *)&quantization, sizeof(uint64_t), qlen, pFile);
	size_t count = rank ? 1 : 0;
	for (int i = 0; i < rank; ++i)
		count *= shape[i];

	//printf("count : %ld]\n", count);

	//NNEF store file format Version 2
	if (version == 2)
	{
		fseek(pFile, header_size, SEEK_SET);
		int result = fread (out->data, sizeof(float), out->size, pFile);
		if (result != out->size) {fputs ("Reading error\n",stderr); assert (0);}
	}
	else if (version == 1)
	{
		int result = fread(out->data, sizeof(float), count, pFile);
		if (result != count) {fputs ("Reading error\n",stderr); assert (0);}
	}
	/*
	   printf("file_name %s\n", file_name);
	   printf("out->n : %d\n", out->n);
	   printf("out->c : %d\n", out->c);
	   printf("out->h : %d\n", out->h);
	   printf("out->w : %d\n", out->w);
	   printf("out->dim : %d\n", out->dim);
	   printf("out->size : %d\n", out->size);
	   printf("out->data[0] : %g\n", out->data[0]);
	 */
	fclose(pFile);
}

void external(tensor * out, int n, int c, int h, int w)
{
    out = make_tensor(out, n, c, h, w);

    printf ("out->n : %d\n", out->n);
    printf ("out->c : %d\n", out->c);
    printf ("out->h : %d\n", out->h);
    printf ("out->w : %d\n", out->w);
    printf ("out->dim : %d\n", out->dim);
    printf ("out->size : %d\n", out->size);
}

void matmul(tensor * out, tensor * in_x, tensor * in_y)
{
#ifdef DEBUG_TIME
    double start = clock();
#endif

    int m = in_x->h; 
    int p = in_x->w;
    int n = in_y->w;
    out = make_tensor(out, 0, 0, m, n);

    // [m*p][p*m] = [m*n]
    for (int i=0; i < m; i++)
    {
        for (int j=0; j < n; j++)
        {
            float sum = 0.0;
            for(int k = 0; k < p; k++)
            {
                sum += in_x->data[where_pos2(in_x, i, k)] * in_y->data[where_pos2(in_y, k, j)];
            }
            out->data[where_pos2(out, i, j)] = sum ;
        }
    }

#ifdef DEBUG_TIME
        double end = clock();
        printf("[matmul time = %1.3f seconds]\n",(end-start)/CLOCKS_PER_SEC);
#endif
}

void matmul_ft(tensor * out, tensor * in_x, tensor * in_y)
{
#ifdef DEBUG_TIME
    double start = clock();
#endif

    int m = in_x->h;
    int p = in_x->w;
    int n = in_y->h;
    out = make_tensor(out, 0, 0, m, n);

    // [m*p][p*m] = [m*n]
    for (int i=0; i < m; i++)
    {
        for (int j=0; j < n; j++)
        {
            float sum = 0.0;
            for(int k = 0; k < p; k++)
            {
                sum += in_x->data[where_pos2(in_x, i, k)] * in_y->data[where_pos2(in_y, j, k)];
            }
            out->data[where_pos2(out, i, j)] = sum ;
        }
    }

#ifdef DEBUG_TIME
        double end = clock();
        printf("[matmul time = %1.3f seconds]\n",(end-start)/CLOCKS_PER_SEC);
#endif
}

void mul(tensor * out, tensor * in_x, float value)
{
#ifdef DEBUG_TIME
    double start = clock();
#endif
    out = make_tensor(out, in_x->n, in_x->c, in_x->h, in_x->w);
    out->dim = in_x->dim;
    for (int i = 0; i < in_x->size; i++)
	out->data[i] = in_x->data[i] * value;

#ifdef DEBUG_TIME
        double end = clock();
        printf("[mul time = %1.3f seconds]\n",(end-start)/CLOCKS_PER_SEC);
#endif
}

void add(tensor * out, tensor * in_x, tensor * in_y)
{
#ifdef DEBUG_TIME
    double start = clock();
#endif

    out = make_tensor(out, in_x->n, in_x->c, in_x->h, in_x->w);
    // Note : that there may be problems with different shapes
	if (in_x->dim == in_y->dim)
	{
		for (int i = 0; i < out->size; i++)
			out->data[i] = in_x->data[i] + in_y->data[i];
	}
	else if ((in_x->dim == 4) &&  (in_y->dim == 2))
	{
		int a_batch = in_x->n;
		int a_channel = in_x->c;
		int a_h = in_x->h;
		int a_w = in_x->w;

		int b_batch = in_y->h;
		int b_channel = in_y->w;
		assert(a_channel == b_channel);
		
		float bvalue = 0;
		for(int ch = 0; ch< a_channel; ch++)
		{
			bvalue = in_y->data[ch];
			for(int h = 0; h < a_h; h++)
			{
				for(int w = 0; w < a_w; w++)
				{
					out->data[ch * out->h * out->w + h * out->w + w] = in_x->data[ch * in_x->h * in_x->w + h * in_x->w + w] + bvalue;
				}
			}
		}
	}
#ifdef DEBUG_TIME
        double end = clock();
        printf("[add time = %1.3f seconds]\n",(end-start)/CLOCKS_PER_SEC);
#endif
}

void softmax(tensor * out, tensor * in_x)
{
#ifdef DEBUG_TIME
    double start = clock();
#endif

    make_tensor(out, in_x->n, in_x->c, in_x->h, in_x->w);
    float sum = 0;

    for (int i = 0; i < out->size; i++)
    {
        out->data[i] = expf(in_x->data[i]);
        sum = sum + out->data[i];
    }

    assert(sum != 0);

    for (int i = 0; i < out->size; i++)
        out->data[i] = out->data[i] / sum;

#ifdef DEBUG_TIME
    double end = clock();
    printf("[softmax time = %1.3f seconds]\n",(end-start)/CLOCKS_PER_SEC);
#endif
}

//reshape = reshape(input_Placeholder, shape = [-1, 28, 28, 1]);
void reshape(tensor * out, tensor * in_x, int n, int c, int h, int w)
{
#ifdef DEBUG_TIME
    double start = clock();
#endif

    make_tensor(out, in_x->n, in_x->c, in_x->h, in_x->w);

    int shapeLength = 1;
    int tensorLength = 1;
    int index = -1;
    int shape[4];
    shape[0] = n;
    shape[1] = c;
    shape[2] = h;
    shape[3] = w;
    tensorLength = in_x->size;

    for (size_t i = 0; i < 4; ++i) 
    {
        if (shape[i] == -1) {
          // no -1 before
          assert(shapeLength > 0);
          index = i;
        }
        if (shape[i] == 0)
            shape[i] = 1;
        shapeLength *= shape[i];
    }

    if (shapeLength < 0) 
    {
        if (shapeLength != tensorLength) 
        {
            shapeLength = abs(shapeLength);
            shape[index] = tensorLength / shapeLength;
            shapeLength = tensorLength;
        }
    }

    assert(tensorLength == shapeLength);

    // Run
    out->n = shape[0];
    out->c = shape[1];
    out->h = shape[2];
    out->w = shape[3];

    if (n == 0)
    {
        out->n = 1;
        out->dim = 3;
    }

    if (c == 0)
    {
        out->c = 1;
        out->dim = 2;
    }

    if (h == 0)
    {
        out->h = 1;
        out->dim = 1;
    }

    if (w == 0)
    {
        out->w = 1;
        out->dim = 0;
    }

    for (int i = 0; i < out->size; i++)
        out->data[i] = in_x->data[i];

#ifdef DEBUG_TIME
    double end = clock();
    printf("[reshape time = %1.3f seconds]\n",(end-start)/CLOCKS_PER_SEC);
#endif
}


void concat(tensor * out, tensor * in_x, tensor * in_y, int axis)
{
#ifdef DEBUG_TIME
    double start = clock();
#endif

    //1-D
    //Chack
    assert (in_x->dim == in_y->dim);

    //Run
    out->n = in_x->n; out->c = in_x->c; out->h = in_x->h; out->w = in_x->w;

    if (axis == 0)
        out->n = out->n + in_y->n;
    else if (axis == 1)
        out->c = out->c + in_y->c;
    else if (axis == 2)
        out->n = out->n + in_y->h;
    else if (axis == 3)
        out->w = out->w + in_y->w;

    make_tensor(out, out->n, out->c, out->h, out->w);

    // push size
    int push_size = 1;
    int run_size = 1;
    for (int i = in_x->dim - 1; i >=axis; i--)
    {
        int shap_num;
        if (i == 0)
            shap_num = in_x->n;
        else if (i == 1)
            shap_num = in_x->c;
        else if (i == 2)
            shap_num = in_x->h;
        else if (i == 3)
            shap_num = in_x->w;
        push_size = push_size * shap_num;
    }
    for (int i = axis - 1; i >=0; i--)
    {
        int shap_num;
        if (i == 0)
            shap_num = in_x->n;
        if (i == 1)
            shap_num = in_x->c;
        if (i == 2)
            shap_num = in_x->h;
        if (i == 3)
            shap_num = in_x->w;
        run_size = run_size * shap_num;
    }

    // total size check
    int cnt = 0;
    for (int i = 0; i < run_size; i++)
    {
        for (int j = 0; j < push_size; j++)
        {
            out->data[cnt] = in_x->data[i * push_size + j];
            cnt++;
        }

        for (int j = 0; j < push_size; j++)
        {
            out->data[cnt] = in_y->data[i * push_size + j];
            cnt++;
        }
    }
#ifdef DEBUG_TIME
    double end = clock();
    printf("[concat time = %1.3f seconds]\n",(end-start)/CLOCKS_PER_SEC);
#endif
}

void squeeze(tensor * out, tensor * in_x, int n, int c, int h, int w)
{
#ifdef DEBUG_TIME
    double start = clock();
#endif

    make_tensor(out, in_x->n, in_x->c, in_x->h, in_x->w);

    int axes_size = 0;
    if (n != 0)
        axes_size++;
    if (c != 0)
        axes_size++;
    if (h != 0)
        axes_size++;
    if (w != 0)
        axes_size++;

    int count = 0;
    int cnt = 0;
    //run
    for (int i = 0; i < in_x->dim; i++)
    {
        if (count < axes_size)
        {
            int axes;
            if (count == 0)
                axes = n;
            if (count == 1)
                axes = c;
            if (count == 2)
                axes = h;
            if (count == 3)
                axes = w;
            if (i == axes)
            {
                count++;
                continue;
                //drop out
            }
        }

        int A_shape;
        if (i == 0)
            A_shape = in_x->n;
        else if (i == 1)
            A_shape = in_x->c;
        else if (i == 2)
            A_shape = in_x->h;
        else if (i == 3)
            A_shape = in_x->w;

        if (cnt == 0)
            out->n = A_shape;
        else if (cnt == 1)
            out->c = A_shape;
        else if (cnt == 2)
            out->h = A_shape;
        else if (cnt == 3)
            out->w = A_shape;
        cnt++;
        //out.shape.push_back(A.shape[i]);
    }

    for (int i = 0; i < in_x->size; i++)
        out->data[i] = in_x->data[i];

#ifdef DEBUG_TIME
    double end = clock();
    printf("[squeeze time = %1.3f seconds]\n",(end-start)/CLOCKS_PER_SEC);
#endif
}

void transpose(tensor * out, tensor * in_x, int n, int c, int h, int w)
{
#ifdef DEBUG_TIME
    double start = clock();
#endif

    make_tensor(out, in_x->n, in_x->c, in_x->h, in_x->w);

    int newA[4];
    int pos[4];
    int transPos[4];
    int shape[4];
    int in_x_shape[4];
    in_x_shape[0] = in_x->n;
    in_x_shape[1] = in_x->c;
    in_x_shape[2] = in_x->h;
    in_x_shape[3] = in_x->w;
    shape[0] = n;
    shape[1] = c;
    shape[2] = h;
    shape[3] = w;

    //newShape 
    out->n = in_x_shape[shape[0]]; 
    out->c = in_x_shape[shape[1]]; 
    out->h = in_x_shape[shape[2]]; 
    out->w = in_x_shape[shape[3]];     

    // init           
    for (int index = 0; index < 4; index++)
        pos[index] = 0;

    int S = 1;
    for (int index = 0; index < 4; index++)
        S = S * in_x_shape[index];

    for (int i = 0; i < S; i++)
    {
        int carryOut = 0;
        int index = i;
        // e.g 2 * 3 * 4, Loop of variable
        for (int j = 0; j < 4; j++)
        {   
            pos[j] = index % in_x_shape[j];
            carryOut = index / in_x_shape[j];
            index = carryOut;
        }

        for (int index = 0; index < 4; index++)
            transPos[index] = pos[shape[index]];

        out->data[where_pos4(out, transPos[0], transPos[1], transPos[2], transPos[3])]
        = in_x->data[where_pos4(in_x, pos[0], pos[1], pos[2], pos[3])];

    }

#ifdef DEBUG_TIME
    double end = clock();
    printf("[transpose time = %1.3f seconds]\n",(end-start)/CLOCKS_PER_SEC);
#endif
}

void conv(tensor * out, tensor * in_x, tensor * filter, tensor * bias, int padding, int stride, int groups)
{
#ifdef DEBUG_TIME
    double start = clock();
#endif
    //shape
    int inPic = in_x->n;
    int filterKernelNum = filter->n;

    assert(in_x->h >= filter->h);
    assert(in_x->w >= filter->w);

    int v_offset_Y = 0;
    int v_offset_X = 0;

    //virtual_height, virtual_weight
    int v_height = 0;
    int v_width = 0;

    //virtual_bound_height , virtual_bound_weight
    int vb_height = 0;
    int vb_width = 0;

    int pad = 0;

    if (padding)
    {
        out->n = in_x->n;
        out->c = filter->n;
        out->h = ceil(((float)in_x->h)/((float)stride));
        out->w = ceil(((float)in_x->w)/((float)stride));
        
        //padding
        int newY = filter->h + (out->h - 1) * stride;
        int newX = filter->w + (out->w - 1) * stride;

        v_offset_Y = (newY - in_x->h) / 2;
        v_offset_X = (newX - in_x->w) / 2;

        vb_height = in_x->h + v_offset_Y;
        vb_width  = in_x->w + v_offset_X;
        
        pad = ((out->h - 1) * stride + filter->h - in_x->h) / 2;
    }
    else
    {
        out->n = in_x->n;
        out->c = filter->n;
        out->h = ceil(((float)(in_x->h - filter->h+ 1))/((float)stride));
        out->w = ceil(((float)(in_x->w - filter->w+ 1))/((float)stride));

        vb_height = in_x->h;
        vb_width  = in_x->w;
        
        pad = 0;
    }

    //virtual_height, virtual_weight
    v_height = v_offset_Y;
    v_width = v_offset_X;

    make_tensor(out, out->n, out->c, out->h, out->w);

#ifdef im2colxGEMM

    int out_w,out_h;
    int workspace_size;

    out_w = out->h;
    out_h = out->w;
    workspace_size = out_h * out_w * filter->h * filter->h * in_x->c;
    float * colD = 0;

    if (!colD) colD = (float *) calloc(workspace_size, sizeof(float));    
    int c,h,w;

    int height_col = out_h;
    int width_col = out_w;
    int channels_col = in_x->c * filter->h * filter->h;

    for (int Pic = 0; Pic < inPic; Pic++)
    {
        for (c = 0; c < channels_col; ++c) 
        {
            for (h = 0; h < height_col; ++h) 
            {
                for (w = 0; w < width_col; ++w) 
                {
                    int w_offset = c % filter->h;
                    int h_offset = (c / filter->h) % filter->h;
                    int c_im = c / filter->h / filter->h;
                    int im_row = h_offset + h * stride;
                    int im_col = w_offset + w * stride;
                    int col_index = (c * height_col + h) * width_col + w;
                    //int col_index = (h * width_col + w) * channels_col + c;
                    colD[col_index] = im2col_get_pixel(in_x , in_x->h, in_x->w, in_x->c, im_row, im_col, c_im, pad);
                }
            }
        }

        int m = filter->n; // input height N
        int n = out_w * out_h; // filter width = number of filter = 9
        int p = filter->c * filter->h * filter->w; // CHW = input width = filter height = channel*ksize*ksize

        for (int i=0; i < m; i++) //2
        {
            for (int j=0; j < n; j++) //9
            {
                float sum = 0.0;
                for(int k = 0; k < p; k++) //18
                {
                    sum += filter->data[i * p + k] * colD[k * n + j];
                }
                out->data[i*n+j] = sum + bias->data[i];
            }
        }

        free(colD);
    }
#else    
    for (int Pic = 0; Pic < inPic; Pic++)
    {
        for (int filterKernel = 0; filterKernel < filterKernelNum; filterKernel++)// 32
        {
            for (int height = 0; height < out->h; height = height + 1)//28
            {
                for (int width = 0; width < out->w; width = width + 1)//28
                {
                    float featureValue = 0;
                    int offsetY = (height * stride);
                    int offsetX = (width  * stride);

                    for (int z = 0; z < filter->c; z++)
                    {
                        for (int y = 0; y < filter->h; y++)
                        {
                             for (int x = 0; x < filter->w; x++)
                             {
                                // logical_height, logical_weight
                                int l_height = y + offsetY;
                                int l_weight = x + offsetX;

                                if ((l_height >= v_height && l_weight >= v_width) && (l_height < vb_height && l_weight < vb_width))
                                    featureValue = featureValue + in_x->data[Pic * in_x->c * in_x->h * in_x->w + z * in_x->h * in_x->w + (l_height - v_offset_Y) * in_x->w + (l_weight - v_offset_X)] * filter->data[filterKernel * filter->c * filter->h * filter->w + z * filter->h * filter->w + y * filter->w + x];
                            }
                        }
                    }
                    out->data[Pic * out->c * out->h * out->w + filterKernel * out->h * out->w + height * out->w + width] = featureValue + bias->data[filterKernel];
                }
            }
        }
    }
#endif    

#ifdef DEBUG_TIME
    double end = clock();
    printf("[conv time = %1.3f seconds]\n",(end-start)/CLOCKS_PER_SEC);
#endif
}
//max_pool = max_pool(relu, border = 'constant', dilation = [], padding = [], size = [1, 1, 2, 2], stride = [1, 1, 2, 2]);
void max_pool(tensor * out, tensor * in_x, int size, int padding, int stride)
{
#ifdef DEBUG_TIME
    double start = clock();
#endif
    //Run
    //tensor<float> out;
    //out.shape.resize(4);

    //Chack
    assert(in_x->h >= size);
    assert(in_x->w >= size);

    int v_offset_T = 0;
    int v_offset_Z = 0;
    int v_offset_Y = 0;
    int v_offset_X = 0;

    //virtual_height, virtual_weight
    int v_height = 0;
    int v_width = 0;

    //virtual_bound_height , virtual_bound_weight
    int vb_height = 0;
    int vb_width = 0;

    if (padding)
    {
        out->n = in_x->n;
        out->c = in_x->c;
        out->h = (int)(ceil((float)(in_x->h)/(float)stride));
        out->w = (int)(ceil((float)(in_x->w)/(float)stride));

        int newY = size + (out->h - 1) * stride;
        int newX = size + (out->w - 1) * stride;

        v_offset_Y = (newY - in_x->h) / 2;
        v_offset_X = (newX - in_x->w) / 2;

        vb_height = in_x->h + v_offset_Y;
        vb_width = in_x->w + v_offset_X;
    }
    else
    {
        out->n = in_x->n;
        out->c = in_x->c;
        out->h = ceil(((float)(in_x->h - size + 1))/((float)stride));
        out->w = ceil(((float)(in_x->w - size + 1))/((float)stride));

        vb_height = in_x->h;
        vb_width = in_x->w;
    }

    //virtual_height, virtual_weight
    v_height = v_offset_Y;
    v_width = v_offset_X;

    make_tensor(out, out->n, out->c, out->h, out->w);

    // Tensor is [batch, height, width, channels], NNEF not
    // NNEF is [batch, channels, height, width]
    for (int N = 0; N < out->n; N++)
        //#pragma omp parallel for
        for (int C = 0; C < out->c; C++)
            for (int H = 0; H < out->h; H++)
                for (int W = 0; W < out->w; W++)
                {
                    float MaxValue = -FLT_MAX;
                    int offsetY = (H  * stride);
                    int offsetX = (W  * stride);

                    //for (int x = 0; x < size[0]; x++)
                        //for (int y = 0; y < size[1]; y++)
                    for (int z = 0; z < size; z++)
                        for (int t = 0; t < size; t++)
                            {
                                // logical_height, logical_weight
                                int l_height = z + offsetY;
                                int l_weight = t + offsetX;

                                if ((l_height >= v_height && l_weight >= v_width) && (l_height < vb_height && l_weight < vb_width))
                                {
                                    float value = in_x->data[N * in_x->c * in_x->h * in_x->w + C * in_x->h * in_x->w + (l_height - v_offset_Y) * in_x->w + (l_weight - v_offset_X)];
                                    if (MaxValue < value)
                                        MaxValue = value;
                                }
                            }
                    out->data[N * out->c * out->h * out->w + C * out->h * out->w + H * out->w + W] = MaxValue;
                }

#ifdef DEBUG_TIME
    double end = clock();
    printf("[maxpool time = %1.3f seconds]\n",(end-start)/CLOCKS_PER_SEC);
#endif
}

void relu(tensor * out, tensor * in_x)
{
#ifdef DEBUG_TIME
    double start = clock();
#endif

    make_tensor(out, in_x->n, in_x->c, in_x->h, in_x->w);
    
    for (int i = 0; i < out->size; i++)
        out->data[i] = fmax(in_x->data[i], 0.0);

#ifdef DEBUG_TIME
    double end = clock();
    printf("[relu time = %1.3f seconds]\n",(end-start)/CLOCKS_PER_SEC);
#endif
}

void relu6(tensor * out, tensor * in_x)
{
#ifdef DEBUG_TIME
    double start = clock();
#endif

    make_tensor(out, in_x->n, in_x->c, in_x->h, in_x->w);

    for (int i = 0; i < out->size; i++)
    {
        out->data[i] = fmin(fmax(in_x->data[i], 0.0), 6.0);
    }
#ifdef DEBUG_TIME
    double end = clock();
    printf("[relu6 time = %1.3f seconds]\n",(end-start)/CLOCKS_PER_SEC);
#endif
}

void sigmoid(tensor * out, tensor * in_x)
{
#ifdef DEBUG_TIME
    double start = clock();
#endif

    make_tensor(out, in_x->n, in_x->c, in_x->h, in_x->w);

    for (int i = 0; i < out->size; i++)
        out->data[i] = 1.0 / (1.0 + expf(-in_x->data[i]));

#ifdef DEBUG_TIME
    double end = clock();
    printf("[sigmoid time = %1.3f seconds]\n",(end-start)/CLOCKS_PER_SEC);
#endif
}

void convxbias(tensor * out, tensor * in_x, tensor * filter, float bias, int padding, int stride, int groups)
{
#ifdef DEBUG_TIME
    double start = clock();
#endif
    //shape
    int inPic = in_x->n;
    int filterKernelNum = filter->n;

    assert(in_x->h >= filter->h);
    assert(in_x->w >= filter->w);

    int v_offset_Y = 0;
    int v_offset_X = 0;

    //virtual_height, virtual_weight
    int v_height = 0;
    int v_width = 0;

    //virtual_bound_height , virtual_bound_weight
    int vb_height = 0;
    int vb_width = 0;

    int pad = 0;

    if (padding)
    {
        out->n = in_x->n;
        out->c = filter->n;
        out->h = ceil(((float)in_x->h)/((float)stride));
        out->w = ceil(((float)in_x->w)/((float)stride));
        
        //padding
        int newY = filter->h + (out->h - 1) * stride;
        int newX = filter->w + (out->w - 1) * stride;

        v_offset_Y = (newY - in_x->h) / 2;
        v_offset_X = (newX - in_x->w) / 2;

        vb_height = in_x->h + v_offset_Y;
        vb_width  = in_x->w + v_offset_X;
        
        pad = ((out->h - 1) * stride + filter->h - in_x->h) / 2;
    }
    else
    {
        out->n = in_x->n;
        out->c = filter->n;
        out->h = ceil(((float)(in_x->h - filter->h+ 1))/((float)stride));
        out->w = ceil(((float)(in_x->w - filter->w+ 1))/((float)stride));

        vb_height = in_x->h;
        vb_width  = in_x->w;
        
        pad = 0;
    }

    //virtual_height, virtual_weight
    v_height = v_offset_Y;
    v_width = v_offset_X;

    make_tensor(out, out->n, out->c, out->h, out->w);

	if(groups ==1) //general convolution
	{
#ifdef im2colxGEMM

		int out_w,out_h;
		int workspace_size;

		out_w = out->h;
		out_h = out->w;
		workspace_size = out_h * out_w * filter->h * filter->h * in_x->c;
		float * colD = 0;
		
		if (!colD) colD = (float *) calloc(workspace_size, sizeof(float));    
		int c,h,w;

		int height_col = out_h;
		int width_col = out_w;
		int channels_col = in_x->c * filter->h * filter->h;
		
		for (int Pic = 0; Pic < inPic; Pic++)
		{
			for (c = 0; c < channels_col; ++c) 
			{
				for (h = 0; h < height_col; ++h) 
				{
					for (w = 0; w < width_col; ++w) 
					{
						int w_offset = c % filter->h;
						int h_offset = (c / filter->h) % filter->h;
						int c_im = c / filter->h / filter->h;
						int im_row = h_offset + h * stride;
						int im_col = w_offset + w * stride;
						int col_index = (c * height_col + h) * width_col + w;
						//int col_index = (h * width_col + w) * channels_col + c;
						colD[col_index] = im2col_get_pixel(in_x , in_x->h, in_x->w, in_x->c, im_row, im_col, c_im, pad);
					}
				}
			}

			int m = filter->n; // input height N
			int n = out_w * out_h; // filter width = number of filter = 9
			int p = filter->c * filter->h * filter->w; // CHW = input width = filter height = channel*ksize*ksize

			for (int i=0; i < m; i++) //2
			{
				for (int j=0; j < n; j++) //9
				{
					float sum = 0.0;
					for(int k = 0; k < p; k++) //18
					{
						// [ik][kj]
						sum += filter->data[i * p + k] * colD[k * n + j];
					}
					out->data[i*n+j] = sum + bias;
				}
			}

			free(colD);
		}
#else    
		for (int Pic = 0; Pic < inPic; Pic++)
		{
			for (int filterKernel = 0; filterKernel < filterKernelNum; filterKernel++)// 32
			{
				for (int height = 0; height < out->h; height = height + 1)//28
				{
					for (int width = 0; width < out->w; width = width + 1)//28
					{
						float featureValue = 0;
						int offsetY = (height * stride);
						int offsetX = (width  * stride);

						for (int z = 0; z < filter->c; z++)
						{
							for (int y = 0; y < filter->h; y++)
							{
								 for (int x = 0; x < filter->w; x++)
								 {
									// logical_height, logical_weight
									int l_height = y + offsetY;
									int l_weight = x + offsetX;

									if ((l_height >= v_height && l_weight >= v_width) && (l_height < vb_height && l_weight < vb_width))
										featureValue = featureValue + in_x->data[Pic * in_x->c * in_x->h * in_x->w + z * in_x->h * in_x->w + (l_height - v_offset_Y) * in_x->w + (l_weight - v_offset_X)] * filter->data[filterKernel * filter->c * filter->h * filter->w + z * filter->h * filter->w + y * filter->w + x];
								}
							}
						}
						out->data[Pic * out->c * out->h * out->w + filterKernel * out->h * out->w + height * out->w + width] = featureValue + bias;
					}
				}
			}
		}
#endif
	}
	else
	{
        int count = 0;
		for (int Pic = 0; Pic < inPic; Pic++)
		{
			for (int filterKernel = 0; filterKernel < filterKernelNum; filterKernel++)// 32
			{
				for (int height = 0; height < out->h; height = height + 1)//28
				{
					for (int width = 0; width < out->w; width = width + 1)//28
					{
						float featureValue = 0;
						int offsetY = (height * stride);
						int offsetX = (width  * stride);

                        for (int y = 0; y < filter->h; y++)
                        {
                             for (int x = 0; x < filter->w; x++)
                             {
                                // logical_height, logical_weight
                                int l_height = y + offsetY;
                                int l_weight = x + offsetX;

                                if ((l_height >= v_height && l_weight >= v_width) && (l_height < vb_height && l_weight < vb_width))
                                {
                                    featureValue = featureValue + in_x->data[Pic * in_x->c * in_x->h * in_x->w + filterKernel * in_x->h * in_x->w + (l_height - v_offset_Y) * in_x->w + (l_weight - v_offset_X)] * filter->data[filterKernel * filter->c * filter->h * filter->w + 0 * filter->h * filter->w + y * filter->w + x];
                                    //colD[count] = in_x->data[Pic * in_x->c * in_x->h * in_x->w + filterKernel * in_x->h * in_x->w + (l_height - v_offset_Y) * in_x->w + (l_weight - v_offset_X)]; 
                                    //colD[count] = colD[count] * filter->data[filterKernel * filter->c * filter->h * filter->w + 0 * filter->h * filter->w + y * filter->w + x];
                                    //featureValue = featureValue + colD[count];                                      
                                }
                            }
                        }
						out->data[Pic * out->c * out->h * out->w + filterKernel * out->h * out->w + height * out->w + width] = featureValue + bias;
					}
				}
			}
		}
	}
#ifdef DEBUG_TIME
    double end = clock();
    printf("[convxbias time = %1.3f seconds, groups = %d]\n",(end-start)/CLOCKS_PER_SEC, groups);
#endif
}

void convxt_bias(tensor * out, tensor * in_x, tensor * filter, tensor * bias, int padding, int stride, int groups)
{
#ifdef DEBUG_TIME
    double start = clock();
#endif
    //shape
    int inPic = in_x->n;
    int filterKernelNum = filter->n;

    assert(in_x->h >= filter->h);
    assert(in_x->w >= filter->w);

    int v_offset_Y = 0;
    int v_offset_X = 0;

    //virtual_height, virtual_weight
    int v_height = 0;
    int v_width = 0;

    //virtual_bound_height , virtual_bound_weight
    int vb_height = 0;
    int vb_width = 0;

    int pad = 0;

    if (padding)
    {
        out->n = in_x->n;
        out->c = filter->n;
        out->h = ceil(((float)in_x->h)/((float)stride));
        out->w = ceil(((float)in_x->w)/((float)stride));

        //padding
        int newY = filter->h + (out->h - 1) * stride;
        int newX = filter->w + (out->w - 1) * stride;

        v_offset_Y = (newY - in_x->h) / 2;
        v_offset_X = (newX - in_x->w) / 2;

        vb_height = in_x->h + v_offset_Y;
        vb_width  = in_x->w + v_offset_X;

        pad = ((out->h - 1) * stride + filter->h - in_x->h) / 2;
    }
    else
    {
        out->n = in_x->n;
        out->c = filter->n;
        out->h = ceil(((float)(in_x->h - filter->h+ 1))/((float)stride));
        out->w = ceil(((float)(in_x->w - filter->w+ 1))/((float)stride));

        vb_height = in_x->h;
        vb_width  = in_x->w;

        pad = 0;
    }

    //virtual_height, virtual_weight
    v_height = v_offset_Y;
    v_width = v_offset_X;

    make_tensor(out, out->n, out->c, out->h, out->w);

	if(groups == 1) //general convolution
	{
#ifdef im2colxGEMM

		int out_w,out_h;
		int workspace_size;

		out_w = out->h;
		out_h = out->w;
		workspace_size = out_h * out_w * filter->h * filter->h * in_x->c;
		float * colD = 0;

		if (!colD) colD = (float *) calloc(workspace_size, sizeof(float));
		int c,h,w;

		int height_col = out_h;
		int width_col = out_w;
		int channels_col = in_x->c * filter->h * filter->h;

		for (int Pic = 0; Pic < inPic; Pic++)
		{
			for (c = 0; c < channels_col; ++c)
			{
				for (h = 0; h < height_col; ++h)
				{
					for (w = 0; w < width_col; ++w)
					{
						int w_offset = c % filter->h;
						int h_offset = (c / filter->h) % filter->h;
						int c_im = c / filter->h / filter->h;
						int im_row = h_offset + h * stride;
						int im_col = w_offset + w * stride;
						int col_index = (c * height_col + h) * width_col + w;
						//int col_index = (h * width_col + w) * channels_col + c;
						colD[col_index] = im2col_get_pixel(in_x , in_x->h, in_x->w, in_x->c, im_row, im_col, c_im, pad);
					}
				}
			}

			int m = filter->n; // input height N
			int n = out_w * out_h; // filter width = number of filter = 9
			int p = filter->c * filter->h * filter->w; // CHW = input width = filter height = channel*ksize*ksize

			for (int i=0; i < m; i++) //2
			{
				for (int j=0; j < n; j++) //9
				{
					float sum = 0.0;
					for(int k = 0; k < p; k++) //18
					{
						// [ik][kj]
						sum += filter->data[i * p + k] * colD[k * n + j];
					}
					out->data[i*n+j] = sum + bias->data[i];
				}
			}
			free(colD);
		}
#else
    assert(0);
#endif
	}
	else
	{
        assert(0);
	}

#ifdef DEBUG_TIME
    double end = clock();
    printf("[convxbias time = %1.3f seconds, groups = %d]\n",(end-start)/CLOCKS_PER_SEC, groups);
#endif
}
void batch_normalization(tensor * out, tensor * in_x, tensor * mean, tensor * variance, tensor * offset, tensor * scale, float epsilon)
{
#ifdef DEBUG_TIME
    double start = clock();
#endif

    out = make_tensor(out, in_x->n, in_x->c, in_x->h, in_x->w);

	for (int B = 0; B < in_x->n; B++)
		for (int C = 0; C < in_x->c; C++)
			for (int H = 0; H < in_x->h; H++)
				for (int W = 0; W < in_x->w; W++)
				{
					int out_index = B * out->c * out->h * out->w + C * out->h * out->w + H * out->w + W;
					int in_index  = B * in_x->c * in_x->h * in_x->w + C * in_x->h * in_x->w + H * in_x->w + W;
					out->data[out_index] = (in_x->data[in_index] - mean->data[C]) / sqrt(variance->data[C] + epsilon);
					out->data[out_index] = scale->data[C] * out->data[out_index] + offset->data[C];
				}

#ifdef DEBUG_TIME
    double end = clock();
    printf("[batch_normalization time = %1.3f seconds]\n",(end-start)/CLOCKS_PER_SEC);
#endif
}

void avg_pool(tensor * out, tensor * in_x, int size, int padding, int stride)
{
#ifdef DEBUG_TIME
    double start = clock();
#endif
    //Run
    //tensor<float> out;
    //out.shape.resize(4);

    //Check
    assert(in_x->h >= size);
    assert(in_x->w >= size);

    int v_offset_T = 0;
    int v_offset_Z = 0;
    int v_offset_Y = 0;
    int v_offset_X = 0;

    //virtual_height, virtual_weight
    int v_height = 0;
    int v_width = 0;

    //virtual_bound_height , virtual_bound_weight
    int vb_height = 0;
    int vb_width = 0;

    if (padding)
    {
        out->n = in_x->n;
        out->c = in_x->c;
        out->h = (int)(ceil((float)(in_x->h)/(float)stride));
        out->w = (int)(ceil((float)(in_x->w)/(float)stride));

        int newY = size + (out->h - 1) * stride;
        int newX = size + (out->w - 1) * stride;

        v_offset_Y = (newY - in_x->h) / 2;
        v_offset_X = (newX - in_x->w) / 2;

        vb_height = in_x->h + v_offset_Y;
        vb_width = in_x->w + v_offset_X;
    }
    else
    {
        out->n = in_x->n;
        out->c = in_x->c;
        out->h = ceil(((float)(in_x->h - size + 1))/((float)stride));
        out->w = ceil(((float)(in_x->w - size + 1))/((float)stride));

        vb_height = in_x->h;
        vb_width = in_x->w;
    }

    //virtual_height, virtual_weight
    v_height = v_offset_Y;
    v_width = v_offset_X;

    make_tensor(out, out->n, out->c, out->h, out->w);

    // Tensor is [batch, height, width, channels], NNEF not
    // NNEF is [batch, channels, height, width]
    for (int N = 0; N < out->n; N++)
        //#pragma omp parallel for
        for (int C = 0; C < out->c; C++)
            for (int H = 0; H < out->h; H++)
                for (int W = 0; W < out->w; W++)
                {
                    float AvgValue = 0.0;
                    int Div = 0;
                    int offsetY = (H  * stride);
                    int offsetX = (W  * stride);
					int out_index = N * out->c * out->h * out->w + C * out->h * out->w + H * out->w + W;
					out->data[out_index] = 0;
                    //for (int x = 0; x < size[0]; x++)
                        //for (int y = 0; y < size[1]; y++)
                    for (int z = 0; z < size; z++)
                        for (int t = 0; t < size; t++)
                            {
                                // logical_height, logical_weight
                                int l_height = z + offsetY;
                                int l_weight = t + offsetX;

                                if ((l_height >= v_height && l_weight >= v_width) && (l_height < vb_height && l_weight < vb_width))
                                {
                                    float value = in_x->data[N * in_x->c * in_x->h * in_x->w + C * in_x->h * in_x->w + (l_height - v_offset_Y) * in_x->w + (l_weight - v_offset_X)];
                                    AvgValue = AvgValue + value;
                                }
                            }
                            out->data[out_index] = AvgValue / (size * size);

                }


#ifdef DEBUG_TIME
    double end = clock();
    printf("[avgpool time = %1.3f seconds]\n",(end-start)/CLOCKS_PER_SEC);
#endif
}

void min(tensor * out, tensor * in_x, float y)
{
#ifdef DEBUG_TIME
    double start = clock();
#endif

    make_tensor(out, in_x->n, in_x->c, in_x->h, in_x->w);

	float tmp;
    for (int i = 0; i < out->size; i++)
	{
        tmp = in_x->data[i];
		out->data[i] = (tmp > y) ? y : tmp;
	}

#ifdef DEBUG_TIME
        double end = clock();
        printf("[min time = %1.3f seconds]\n",(end-start)/CLOCKS_PER_SEC);
#endif
}

void bn_sqrt(tensor * out, tensor * in_x, float epsilon)
{
#ifdef DEBUG_TIME
    double start = clock();
#endif

    out = make_tensor(out, in_x->n, in_x->c, in_x->h, in_x->w);

    for (int i = 0; i < in_x->size; i++)
    {
        out->data[i] = sqrt(in_x->data[i] + epsilon);
    }

#ifdef DEBUG_TIME
        double end = clock();
        printf("[bn_sqrt time = %1.3f seconds]\n",(end-start)/CLOCKS_PER_SEC);
#endif
}

void bn_sub(tensor * out, tensor * in_x, tensor * in_y)
{
#ifdef DEBUG_TIME
    double start = clock();
#endif

    // Note : that there may be problems with different shapes
    out = make_tensor(out, in_x->n, in_x->c, in_x->h, in_x->w);
    for (int B = 0; B < in_x->n; B++)
        for (int C = 0; C < in_x->c; C++)
            for (int H = 0; H < in_x->h; H++)
                for (int W = 0; W < in_x->w; W++)
                {
                    out->data[where_pos4(out, B, C, H, W)] = in_x->data[where_pos4(in_x, B, C, H, W)] - in_y->data[C];
                }

#ifdef DEBUG_TIME
    double end = clock();
        printf("[bn_sub time = %1.3f seconds]\n",(end-start)/CLOCKS_PER_SEC);
#endif
}

void bn_div(tensor * out, tensor * in_x, tensor * in_y)
{
#ifdef DEBUG_TIME
    double start = clock();
#endif

    // Note : that there may be problems with different shapes
    out = make_tensor(out, in_x->n, in_x->c, in_x->h, in_x->w);
    for (int B = 0; B < in_x->n; B++)
        for (int C = 0; C < in_x->c; C++)
            for (int H = 0; H < in_x->h; H++)
                for (int W = 0; W < in_x->w; W++)
                {
                    out->data[where_pos4(out, B, C, H, W)] = in_x->data[where_pos4(in_x, B, C, H, W)] / in_y->data[C];
                }

#ifdef DEBUG_TIME
    double end = clock();
        printf("[bn_div time = %1.3f seconds]\n",(end-start)/CLOCKS_PER_SEC);
#endif
}

void bn_mul(tensor * out, tensor * in_x, tensor * in_y)
{
#ifdef DEBUG_TIME
    double start = clock();
#endif

    // Note : that there may be problems with different shapes
    out = make_tensor(out, in_x->n, in_x->c, in_x->h, in_x->w);
    for (int B = 0; B < in_x->n; B++)
        for (int C = 0; C < in_x->c; C++)
            for (int H = 0; H < in_x->h; H++)
                for (int W = 0; W < in_x->w; W++)
                {
                    out->data[where_pos4(out, B, C, H, W)] = in_x->data[where_pos4(in_x, B, C, H, W)] * in_y->data[C];
                }
#ifdef DEBUG_TIME
    double end = clock();
        printf("[bn_mul time = %1.3f seconds]\n",(end-start)/CLOCKS_PER_SEC);
#endif
}

void bn_add(tensor * out, tensor * in_x, tensor * in_y)
{
#ifdef DEBUG_TIME
    double start = clock();
#endif

    out = make_tensor(out, in_x->n, in_x->c, in_x->h, in_x->w);
    for (int B = 0; B < in_x->n; B++)
        for (int C = 0; C < in_x->c; C++)
            for (int H = 0; H < in_x->h; H++)
                for (int W = 0; W < in_x->w; W++)
                {
                    out->data[where_pos4(out, B, C, H, W)] = in_x->data[where_pos4(in_x, B, C, H, W)] + in_y->data[C];
                }
#ifdef DEBUG_TIME
    double end = clock();
        printf("[bn_add time = %1.3f seconds]\n",(end-start)/CLOCKS_PER_SEC);
#endif
}
