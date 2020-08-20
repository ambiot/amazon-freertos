/**************************************************************************//**
 * @file     main.c
 * @brief    Demo user defined flash secure data protection.
 * @version  V1.00
 * @date     2019-07-09
 *
 * @note
 *
 ******************************************************************************
 *
 * Copyright(c) 2007 - 2019 Realtek Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the License); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ******************************************************************************/

void secdata_load_user_data(void);
void secdata_write_demo_cert(void);

int main(void)
{
    /* write demo cert */
    secdata_write_demo_cert();
     
    /* load user secure data */
    secdata_load_user_data();
	
	while(1);
}